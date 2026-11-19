#include "canli.h"

#include "../edgeaccount.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QBuffer>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextToSpeech>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVoice>
#include <QtEndian>
#include <cmath>

namespace {
// Görüntü isteyen ifadeler: "bu ne", "kameraya bak", "ekranda ne var", "elimdeki ne"…
const QRegularExpression GORSEL_ISTEK(
    QStringLiteral("(kamera|bak|gör|görüyor|elimde|elimdeki|arkamda|üstümde|bu ne|bunu tanı|nasıl görünüyorum|"
                   "ekran|okur musun|ne yazıyor|neresi|hangi renk|kaç tane|şunu|şuna)"),
    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);

constexpr double ESIK_BASLA = 0.04;  // konuşma başı (RMS, 0..1)
constexpr double ESIK_SUR = 0.025;   // konuşma sürüyor sayılır
constexpr int SESSIZLIK_MS = 950;    // bu kadar sessizlikte konuşma biter
constexpr int EN_UZUN_MS = 30000;
constexpr int EN_KISA_MS = 450;
}

Canli::Canli(QObject *parent)
    : QObject(parent), m_ag(new QNetworkAccessManager(this))
{
    m_kareZamanAsimi.setSingleShot(true);
    m_kareZamanAsimi.setInterval(3000);
    connect(&m_kareZamanAsimi, &QTimer::timeout, this, [this] {
        // Kamera kare vermediyse soru görüntüsüz gider
        if (m_bekleyen == Bekleyen::Soru) {
            m_bekleyen = Bekleyen::Yok;
            soruyuGonder(QString());
        } else {
            m_bekleyen = Bekleyen::Yok;
        }
    });
    connect(&m_izleZamanlayici, &QTimer::timeout, this, [this] {
        if (!m_goruntuAcik || m_bekleyen != Bekleyen::Yok || m_durum == QLatin1String("kayit")
            || m_durum == QLatin1String("dusunuyor") || m_durum == QLatin1String("konusuyor"))
            return;
        m_bekleyen = Bekleyen::Izle;
        m_kareZamanAsimi.start();
        Q_EMIT kareIste();
    });
}

Canli::~Canli()
{
    mikrofonKapat();
}

QString Canli::sunucu() const
{
    // Masaüstü Sohbet modu ORBITAL_WEB_URL (…/v1) kullanır; Canlı aynı sunucunun /api/canli uçlarını
    QString u = qEnvironmentVariable("ORBITAL_WEB_URL", QStringLiteral("https://yz.oedge.xyz/v1"));
    u.remove(QRegularExpression(QStringLiteral("/v1/?$")));
    while (u.endsWith(QLatin1Char('/'))) u.chop(1);
    return u;
}

void Canli::durumAyarla(const QString &d)
{
    if (m_durum == d) return;
    m_durum = d;
    Q_EMIT durumDegisti();
}

void Canli::uyar(const QString &m)
{
    m_uyari = m;
    Q_EMIT uyariDegisti();
}

void Canli::setDinle(bool v)
{
    if (m_dinle == v) return;
    m_dinle = v;
    if (!v && m_kayitta) { m_kayitta = false; m_kayit.clear(); durumAyarla(QStringLiteral("dinliyor")); }
    Q_EMIT durumDegisti();
}

void Canli::setSesli(bool v)
{
    if (m_sesli == v) return;
    m_sesli = v;
    if (!v) sozuKes();
    Q_EMIT durumDegisti();
}

void Canli::setGoruntuAcik(bool v)
{
    if (m_goruntuAcik == v) return;
    m_goruntuAcik = v;
    if (!v) setIzle(false);
    Q_EMIT durumDegisti();
}

void Canli::setIzle(bool v)
{
    if (v && !m_goruntuAcik) { uyar(QStringLiteral("Sürekli izleme için önce kamerayı aç.")); v = false; }
    if (m_izle == v) { Q_EMIT durumDegisti(); return; }
    m_izle = v;
    if (v) { m_izleZamanlayici.start(m_izleAralik * 1000); m_sonGozlem.clear(); }
    else m_izleZamanlayici.stop();
    Q_EMIT durumDegisti();
}

void Canli::setIzleAralik(int saniye)
{
    saniye = qBound(8, saniye, 120);
    if (m_izleAralik == saniye) return;
    m_izleAralik = saniye;
    if (m_izle) m_izleZamanlayici.start(m_izleAralik * 1000);
    Q_EMIT durumDegisti();
}

// ---------------------------------------------------------------- oturum
void Canli::baslat()
{
    if (m_aktif) return;
    m_aktif = true;
    uyar(QString());
    mikrofonAc();
    durumAyarla(QStringLiteral("dinliyor"));
    Q_EMIT durumDegisti();
    if (m_mesajlar.isEmpty()) {
        const QString selam = QStringLiteral("Merhaba, ben ORBITAL. Seni dinliyorum.");
        mesajEkle(QStringLiteral("orbital"), selam);
        seslendir(selam);
    }
}

void Canli::bitir()
{
    m_aktif = false;
    sozuKes();
    mikrofonKapat();
    setIzle(false);
    m_kayitta = false;
    m_kayit.clear();
    m_seviye = 0;
    Q_EMIT seviyeDegisti();
    durumAyarla(QStringLiteral("bekliyor"));
    Q_EMIT durumDegisti();
}

void Canli::temizle()
{
    m_mesajlar.clear();
    m_gecmis.clear();
    Q_EMIT mesajlarDegisti();
}

void Canli::sozuKes()
{
    if (m_tts && m_tts->state() == QTextToSpeech::Speaking) m_tts->stop();
    if (m_aktif && m_durum == QLatin1String("konusuyor")) durumAyarla(QStringLiteral("dinliyor"));
}

// ---------------------------------------------------------------- mikrofon ve konuşma algılama
void Canli::mikrofonAc()
{
    if (m_kaynak) return;
    const QAudioDevice cihaz = QMediaDevices::defaultAudioInput();
    if (cihaz.isNull()) { uyar(QStringLiteral("Mikrofon bulunamadı. Yazarak da konuşabilirsin.")); return; }
    QAudioFormat bicim;
    bicim.setSampleRate(16000);
    bicim.setChannelCount(1);
    bicim.setSampleFormat(QAudioFormat::Int16);
    if (!cihaz.isFormatSupported(bicim)) {
        bicim = cihaz.preferredFormat();
        if (bicim.sampleFormat() != QAudioFormat::Float) bicim.setSampleFormat(QAudioFormat::Int16);
    }
    m_ornekHizi = bicim.sampleRate();
    m_kanal = qMax(1, bicim.channelCount());
    m_float = bicim.sampleFormat() == QAudioFormat::Float;
    m_kaynak = new QAudioSource(cihaz, bicim, this);
    m_kaynak->setBufferSize(m_ornekHizi * m_kanal * (m_float ? 4 : 2) / 5);
    m_akis = m_kaynak->start();
    if (!m_akis) { uyar(QStringLiteral("Mikrofon açılamadı. Yazarak da konuşabilirsin.")); m_kaynak->deleteLater(); return; }
    connect(m_akis, &QIODevice::readyRead, this, &Canli::sesOku);
}

void Canli::mikrofonKapat()
{
    if (m_kaynak) { m_kaynak->stop(); m_kaynak->deleteLater(); }
    m_kaynak = nullptr;
    m_akis = nullptr;
}

void Canli::sesOku()
{
    if (!m_akis) return;
    const QByteArray ham = m_akis->readAll();
    if (ham.isEmpty()) return;

    // 16 bit mono'ya çevir ve RMS hesapla
    const int ornekBoy = m_float ? 4 : 2;
    const int kare = ornekBoy * m_kanal;
    const int n = ham.size() / kare;
    QByteArray mono(n * 2, Qt::Uninitialized);
    auto *cikis = reinterpret_cast<qint16 *>(mono.data());
    double toplam = 0;
    for (int i = 0; i < n; ++i) {
        double ornek;
        const char *p = ham.constData() + i * kare;
        if (m_float) ornek = qFromLittleEndian<float>(p);
        else ornek = qFromLittleEndian<qint16>(p) / 32768.0;
        ornek = qBound(-1.0, ornek, 1.0);
        cikis[i] = qint16(ornek * 32767);
        toplam += ornek * ornek;
    }
    const double rms = n ? std::sqrt(toplam / n) : 0;
    m_seviye = m_seviye * 0.6 + rms * 0.4;
    Q_EMIT seviyeDegisti();

    if (!m_aktif || !m_dinle) return;
    const qint64 simdi = QDateTime::currentMSecsSinceEpoch();

    if (m_durum == QLatin1String("dinliyor")) {
        // Konuşma başlamadan önceki ~300 ms'yi tut
        m_onceki.append(mono);
        const int tut = m_ornekHizi * 2 * 3 / 10;
        if (m_onceki.size() > tut) m_onceki.remove(0, m_onceki.size() - tut);
        m_esikUstu = rms > ESIK_BASLA ? m_esikUstu + 1 : 0;
        if (m_esikUstu >= 2) {
            m_kayitta = true;
            m_kayit = m_onceki;
            m_onceki.clear();
            m_konusmaBasi = m_sonSes = simdi;
            durumAyarla(QStringLiteral("kayit"));
        }
    } else if (m_durum == QLatin1String("kayit") && m_kayitta) {
        m_kayit.append(mono);
        if (rms > ESIK_SUR) m_sonSes = simdi;
        if (simdi - m_sonSes > SESSIZLIK_MS || simdi - m_konusmaBasi > EN_UZUN_MS) kayitBitir();
    }
}

void Canli::kayitBitir()
{
    m_kayitta = false;
    const qint64 sure = QDateTime::currentMSecsSinceEpoch() - m_konusmaBasi;
    const QByteArray pcm = m_kayit;
    m_kayit.clear();
    m_esikUstu = 0;
    if (sure < EN_KISA_MS || pcm.size() < m_ornekHizi) { durumAyarla(QStringLiteral("dinliyor")); return; }
    durumAyarla(QStringLiteral("dusunuyor"));
    sesGonder(pcm);
}

QByteArray Canli::wavYap(const QByteArray &pcm) const
{
    QByteArray w;
    QBuffer b(&w);
    b.open(QIODevice::WriteOnly);
    auto u32 = [&](quint32 v) { const quint32 le = qToLittleEndian(v); b.write(reinterpret_cast<const char *>(&le), 4); };
    auto u16 = [&](quint16 v) { const quint16 le = qToLittleEndian(v); b.write(reinterpret_cast<const char *>(&le), 2); };
    b.write("RIFF"); u32(36 + pcm.size()); b.write("WAVE");
    b.write("fmt "); u32(16); u16(1); u16(1); u32(m_ornekHizi); u32(m_ornekHizi * 2); u16(2); u16(16);
    b.write("data"); u32(pcm.size()); b.write(pcm);
    return w;
}

// Whisper sessizlik ya da gürültüde sık uydurduğu kalıplar: bunlar soru sayılmaz.
static bool uydurmaMi(const QString &metin)
{
    static const QRegularExpression KALIP(QStringLiteral(
        "^(altyaz[ıi].*|izledi[gğ]iniz i[cç]in te[sş]ekk[uü]r.*|abone ol.*|te[sş]ekk[uü]rler\\W*|"
        "thank(s| you).*|thanks for watching.*|we should continue\\W*|you\\W*|bye\\W*|"
        "\\W*(m[uü]zik|music|alk[iı][sş])\\W*|\\.+|…)$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const QString m = metin.trimmed();
    return m.size() < 2 || KALIP.match(m).hasMatch();
}

void Canli::sesGonder(const QByteArray &pcm)
{
    QNetworkRequest istek(QUrl(sunucu() + QStringLiteral("/api/canli/dinle")));
    istek.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    const QString jeton = EdgeAccount::token();
    if (!jeton.isEmpty()) istek.setRawHeader("Authorization", "Bearer " + jeton.toUtf8());
    QNetworkReply *y = m_ag->post(istek, wavYap(pcm));
    connect(y, &QNetworkReply::finished, this, [this, y] {
        y->deleteLater();
        const QJsonObject v = QJsonDocument::fromJson(y->readAll()).object();
        if (y->error() != QNetworkReply::NoError) {
            uyar(v.value(QStringLiteral("error")).toString(QStringLiteral("Söylediğin anlaşılamadı.")));
            if (m_aktif) durumAyarla(QStringLiteral("dinliyor"));
            return;
        }
        const QString metin = v.value(QStringLiteral("metin")).toString().trimmed();
        if (metin.isEmpty() || uydurmaMi(metin)) { if (m_aktif) durumAyarla(QStringLiteral("dinliyor")); return; }
        sor(metin);
    });
}

// ---------------------------------------------------------------- ORBITAL'e sor
void Canli::yaziyla(const QString &metin)
{
    const QString m = metin.trimmed();
    if (m.isEmpty()) return;
    if (!m_aktif) { m_aktif = true; mikrofonAc(); Q_EMIT durumDegisti(); }
    sor(m);
}

void Canli::simdiBak()
{
    if (!m_goruntuAcik) { uyar(QStringLiteral("Önce kamerayı aç.")); return; }
    if (!m_aktif) { m_aktif = true; mikrofonAc(); Q_EMIT durumDegisti(); }
    sor(QStringLiteral("Şu an ne görüyorsun? Kısaca anlat."), true);
}

void Canli::sor(const QString &metin, bool kareZorla)
{
    uyar(QString());
    mesajEkle(QStringLiteral("kullanici"), metin);
    durumAyarla(QStringLiteral("dusunuyor"));
    if (m_goruntuAcik && (kareZorla || GORSEL_ISTEK.match(metin).hasMatch())) {
        m_bekleyen = Bekleyen::Soru;
        m_bekleyenSoru = metin;
        m_kareZamanAsimi.start();
        Q_EMIT kareIste();
        return;
    }
    m_bekleyenSoru = metin;
    soruyuGonder(QString());
}

void Canli::kareAl(QObject *videoSink)
{
    auto *sink = qobject_cast<QVideoSink *>(videoSink);
    kareGeldi(sink ? sink->videoFrame().toImage() : QImage());
}

void Canli::kareGeldi(const QImage &kare)
{
    m_kareZamanAsimi.stop();
    const Bekleyen ne = m_bekleyen;
    m_bekleyen = Bekleyen::Yok;
    if (ne == Bekleyen::Yok || kare.isNull()) {
        if (ne == Bekleyen::Soru) soruyuGonder(QString());
        return;
    }
    QImage k = kare;
    if (qMax(k.width(), k.height()) > 1024) k = k.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray jpg;
    QBuffer b(&jpg);
    b.open(QIODevice::WriteOnly);
    k.save(&b, "JPEG", 80);
    const QString dataUrl = QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpg.toBase64());
    if (ne == Bekleyen::Soru) soruyuGonder(dataUrl);
    else izleGonder(dataUrl);
}

static QNetworkRequest canliIstek(const QString &url)
{
    QNetworkRequest istek{QUrl(url)};
    istek.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QString jeton = EdgeAccount::token();
    if (!jeton.isEmpty()) istek.setRawHeader("Authorization", "Bearer " + jeton.toUtf8());
    istek.setTransferTimeout(120000);
    return istek;
}

void Canli::soruyuGonder(const QString &kareDataUrl)
{
    const QString soru = m_bekleyenSoru;
    QJsonArray gecmis;
    for (const QVariant &m : std::as_const(m_gecmis)) gecmis.append(QJsonObject::fromVariantMap(m.toMap()));
    QJsonObject govde{{QStringLiteral("gecmis"), gecmis}, {QStringLiteral("soru"), soru}, {QStringLiteral("kip"), QStringLiteral("soru")}};
    if (!kareDataUrl.isEmpty()) govde.insert(QStringLiteral("kare"), kareDataUrl);
    QNetworkReply *y = m_ag->post(canliIstek(sunucu() + QStringLiteral("/api/canli/sor")), QJsonDocument(govde).toJson(QJsonDocument::Compact));
    const bool kareVar = !kareDataUrl.isEmpty();
    connect(y, &QNetworkReply::finished, this, [this, y, soru, kareVar] {
        y->deleteLater();
        const QJsonObject v = QJsonDocument::fromJson(y->readAll()).object();
        if (y->error() != QNetworkReply::NoError || v.value(QStringLiteral("yanit")).toString().isEmpty()) {
            QString h = v.value(QStringLiteral("error")).toString(QStringLiteral("ORBITAL yanıt veremedi. İnternet bağlantını kontrol et."));
            if (v.value(QStringLiteral("kilitli")).toBool()) h += QStringLiteral(" (hesap.oedge.xyz/abonelik)");
            mesajEkle(QStringLiteral("orbital"), h);
            if (m_aktif) durumAyarla(QStringLiteral("dinliyor"));
            return;
        }
        const QString yanit = v.value(QStringLiteral("yanit")).toString();
        m_gecmis.append(QVariantMap{{QStringLiteral("rol"), QStringLiteral("kullanici")}, {QStringLiteral("metin"), kareVar ? soru + QStringLiteral(" [kamera görüntüsü gösterildi]") : soru}});
        m_gecmis.append(QVariantMap{{QStringLiteral("rol"), QStringLiteral("orbital")}, {QStringLiteral("metin"), yanit}});
        while (m_gecmis.size() > 16) m_gecmis.removeFirst();
        mesajEkle(QStringLiteral("orbital"), yanit, v.value(QStringLiteral("kaynaklar")).toArray().toVariantList());
        seslendir(yanit);
    });
}

void Canli::izleGonder(const QString &kareDataUrl)
{
    const QJsonObject govde{{QStringLiteral("kip"), QStringLiteral("izle")}, {QStringLiteral("kare"), kareDataUrl}, {QStringLiteral("onceki"), m_sonGozlem}};
    QNetworkReply *y = m_ag->post(canliIstek(sunucu() + QStringLiteral("/api/canli/sor")), QJsonDocument(govde).toJson(QJsonDocument::Compact));
    connect(y, &QNetworkReply::finished, this, [this, y] {
        y->deleteLater();
        const QJsonObject v = QJsonDocument::fromJson(y->readAll()).object();
        if (y->error() != QNetworkReply::NoError) {
            uyar(v.value(QStringLiteral("error")).toString(QStringLiteral("İzleme durdu.")));
            setIzle(false);
            return;
        }
        const QString g = v.value(QStringLiteral("yanit")).toString().trimmed();
        if (g.isEmpty() || QRegularExpression(QStringLiteral("^[-–—.\\s]+$")).match(g).hasMatch()) return;
        m_sonGozlem = g;
        mesajEkle(QStringLiteral("orbital"), g);
        if (m_durum == QLatin1String("dinliyor") || m_durum == QLatin1String("bekliyor")) seslendir(g);
    });
}

void Canli::mesajEkle(const QString &rol, const QString &metin, const QVariantList &kaynaklar)
{
    m_mesajlar.append(QVariantMap{{QStringLiteral("rol"), rol}, {QStringLiteral("metin"), metin}, {QStringLiteral("kaynaklar"), kaynaklar}});
    while (m_mesajlar.size() > 40) m_mesajlar.removeFirst();
    Q_EMIT mesajlarDegisti();
}

// ---------------------------------------------------------------- sesli okuma
void Canli::seslendir(const QString &metin)
{
    if (!m_sesli || metin.isEmpty()) { if (m_aktif) durumAyarla(QStringLiteral("dinliyor")); return; }
    if (!m_tts) {
        m_tts = new QTextToSpeech(this);
        m_tts->setLocale(QLocale(QLocale::Turkish, QLocale::Turkey));
        const auto sesler = m_tts->availableVoices();
        for (const QVoice &s : sesler) {
            if (s.locale().language() == QLocale::Turkish) { m_tts->setVoice(s); break; }
        }
        m_tts->setRate(0.05);
        connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State s) {
            if ((s == QTextToSpeech::Ready || s == QTextToSpeech::Error) && m_durum == QLatin1String("konusuyor"))
                durumAyarla(m_aktif ? QStringLiteral("dinliyor") : QStringLiteral("bekliyor"));
        });
    }
    if (m_tts->state() == QTextToSpeech::Error) { if (m_aktif) durumAyarla(QStringLiteral("dinliyor")); return; }
    // Okunurken mikrofon yanıtı yeniden duymasın: konuşma algılama "konusuyor" durumunda çalışmaz
    durumAyarla(QStringLiteral("konusuyor"));
    m_tts->say(metin.left(1500));
}
