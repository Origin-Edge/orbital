#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class QAudioSource;
class QIODevice;
class QNetworkAccessManager;
class QTextToSpeech;

// ORBITAL Canlı: elle dokunmadan sesli konuşma.
// Mikrofon sürekli dinlenir; ses seviyesiyle konuşmanın başı ve sonu bulunur, parça yz.oedge.xyz
// /api/canli/dinle ile yazıya dökülür, /api/canli/sor ile ORBITAL'e sorulur, yanıt Türkçe sesle okunur.
// Kamera görüntüsü QML'deki ImageCapture'dan gelir: soru görüntü istiyorsa (ya da "Şimdi bak" / sürekli
// izleme) kareIste() yayılır, QML kareGeldi(QImage) ile kareyi verir.
class Canli : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool aktif READ aktif NOTIFY durumDegisti)
    Q_PROPERTY(QString durum READ durum NOTIFY durumDegisti)
    Q_PROPERTY(bool dinle READ dinle WRITE setDinle NOTIFY durumDegisti)
    Q_PROPERTY(bool sesli READ sesli WRITE setSesli NOTIFY durumDegisti)
    Q_PROPERTY(bool goruntuAcik READ goruntuAcik WRITE setGoruntuAcik NOTIFY durumDegisti)
    Q_PROPERTY(bool izle READ izle WRITE setIzle NOTIFY durumDegisti)
    Q_PROPERTY(int izleAralik READ izleAralik WRITE setIzleAralik NOTIFY durumDegisti)
    Q_PROPERTY(double seviye READ seviye NOTIFY seviyeDegisti)
    Q_PROPERTY(QVariantList mesajlar READ mesajlar NOTIFY mesajlarDegisti)
    Q_PROPERTY(QString uyari READ uyari NOTIFY uyariDegisti)

public:
    explicit Canli(QObject *parent = nullptr);
    ~Canli() override;

    bool aktif() const { return m_aktif; }
    QString durum() const { return m_durum; }
    bool dinle() const { return m_dinle; }
    bool sesli() const { return m_sesli; }
    bool goruntuAcik() const { return m_goruntuAcik; }
    bool izle() const { return m_izle; }
    int izleAralik() const { return m_izleAralik; }
    double seviye() const { return m_seviye; }
    QVariantList mesajlar() const { return m_mesajlar; }
    QString uyari() const { return m_uyari; }

    void setDinle(bool v);
    void setSesli(bool v);
    void setGoruntuAcik(bool v);
    void setIzle(bool v);
    void setIzleAralik(int saniye);

    Q_INVOKABLE void baslat();
    Q_INVOKABLE void bitir();
    Q_INVOKABLE void sozuKes();
    Q_INVOKABLE void yaziyla(const QString &metin);
    Q_INVOKABLE void simdiBak();
    Q_INVOKABLE void kareGeldi(const QImage &kare);
    Q_INVOKABLE void kareAl(QObject *videoSink); // QML VideoOutput.videoSink'ten son kare
    Q_INVOKABLE void temizle();

Q_SIGNALS:
    void durumDegisti();
    void seviyeDegisti();
    void mesajlarDegisti();
    void uyariDegisti();
    void kareIste();

private:
    enum class Bekleyen { Yok, Soru, Izle };

    void durumAyarla(const QString &d);
    void uyar(const QString &m);
    void mikrofonAc();
    void mikrofonKapat();
    void sesOku();
    void kayitBitir();
    void sesGonder(const QByteArray &pcm);
    void sor(const QString &metin, bool kareZorla = false);
    void soruyuGonder(const QString &kareDataUrl);
    void izleGonder(const QString &kareDataUrl);
    void mesajEkle(const QString &rol, const QString &metin, const QVariantList &kaynaklar = {});
    void seslendir(const QString &metin);
    QString sunucu() const;
    QByteArray wavYap(const QByteArray &pcm) const;

    QNetworkAccessManager *m_ag;
    QTextToSpeech *m_tts = nullptr;
    QPointer<QAudioSource> m_kaynak;
    QPointer<QIODevice> m_akis;
    int m_ornekHizi = 16000;
    int m_kanal = 1;
    bool m_float = false;

    bool m_aktif = false;
    bool m_dinle = true;
    bool m_sesli = true;
    bool m_goruntuAcik = false;
    bool m_izle = false;
    int m_izleAralik = 15;
    QString m_durum = QStringLiteral("bekliyor");
    double m_seviye = 0;
    QVariantList m_mesajlar;
    QVariantList m_gecmis; // [{rol, metin}] sunucuya giden sohbet geçmişi
    QString m_uyari;

    // konuşma algılama
    QByteArray m_onceki;   // konuşma başlamadan önceki ~300 ms (baş kesilmesin)
    QByteArray m_kayit;    // konuşma süresince biriken 16 bit mono PCM
    bool m_kayitta = false;
    int m_esikUstu = 0;
    qint64 m_konusmaBasi = 0;
    qint64 m_sonSes = 0;

    // görüntü
    Bekleyen m_bekleyen = Bekleyen::Yok;
    QString m_bekleyenSoru;
    QString m_sonGozlem;
    QTimer m_kareZamanAsimi;
    QTimer m_izleZamanlayici;
};
