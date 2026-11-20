#include <QCoreApplication>
#include "chatmodel.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include "../paths.h"

namespace {

// Browser tools arrive as "<server>_browser_<action>" from the web browser plugin.
QString browserAction(const QString &name)
{
    const int at = name.indexOf(QStringLiteral("browser_"));
    return at < 0 ? QString() : name.mid(at + 8);
}

QString toolTitle(const QString &name)
{
    const QString browser = browserAction(name);
    if (!browser.isEmpty()) {
        static const QHash<QString, QString> actions{
            {QStringLiteral("navigate"), QCoreApplication::translate("Orbital", "Sayfa açıldı")},
            {QStringLiteral("navigate_back"), QCoreApplication::translate("Orbital", "Geri gidildi")},
            {QStringLiteral("snapshot"), QCoreApplication::translate("Orbital", "Sayfa okundu")},
            {QStringLiteral("take_screenshot"), QCoreApplication::translate("Orbital", "Ekran görüntüsü alındı")},
            {QStringLiteral("click"), QCoreApplication::translate("Orbital", "Tıklandı")},
            {QStringLiteral("type"), QCoreApplication::translate("Orbital", "Yazıldı")},
            {QStringLiteral("fill_form"), QCoreApplication::translate("Orbital", "Form dolduruldu")},
            {QStringLiteral("select_option"), QCoreApplication::translate("Orbital", "Seçim yapıldı")},
            {QStringLiteral("press_key"), QCoreApplication::translate("Orbital", "Tuşa basıldı")},
            {QStringLiteral("hover"), QCoreApplication::translate("Orbital", "İmleç getirildi")},
            {QStringLiteral("wait_for"), QCoreApplication::translate("Orbital", "Sayfa beklendi")},
            {QStringLiteral("tabs"), QCoreApplication::translate("Orbital", "Sekmeler")},
            {QStringLiteral("find"), QCoreApplication::translate("Orbital", "Sayfada arandı")},
            {QStringLiteral("close"), QCoreApplication::translate("Orbital", "Tarayıcı kapatıldı")},
        };
        return actions.value(browser, QCoreApplication::translate("Orbital", "Tarayıcı"));
    }
    static const QHash<QString, QString> titles{
        {QStringLiteral("read"), QCoreApplication::translate("Orbital", "Dosya okundu")},
        {QStringLiteral("write"), QCoreApplication::translate("Orbital", "Dosya yazıldı")},
        {QStringLiteral("edit"), QCoreApplication::translate("Orbital", "Dosya düzenlendi")},
        {QStringLiteral("bash"), QCoreApplication::translate("Orbital", "Komut çalıştırıldı")},
        {QStringLiteral("shell"), QCoreApplication::translate("Orbital", "Komut çalıştırıldı")},
        {QStringLiteral("list"), QCoreApplication::translate("Orbital", "Klasör listelendi")},
        {QStringLiteral("glob"), QCoreApplication::translate("Orbital", "Dosyalar arandı")},
        {QStringLiteral("grep"), QCoreApplication::translate("Orbital", "İçerik arandı")},
        {QStringLiteral("skill"), QCoreApplication::translate("Orbital", "Skill yüklendi")},
        {QStringLiteral("webfetch"), QCoreApplication::translate("Orbital", "Web sayfası okundu")},
        {QStringLiteral("todowrite"), QCoreApplication::translate("Orbital", "Yapılacaklar güncellendi")},
        {QStringLiteral("task"), QCoreApplication::translate("Orbital", "Alt görev")},
    };
    return titles.value(name, name);
}

QString toolIcon(const QString &name)
{
    if (!browserAction(name).isEmpty())
        return QStringLiteral("globe");
    if (name == QLatin1String("read"))
        return QStringLiteral("file");
    if (name == QLatin1String("write") || name == QLatin1String("edit") || name == QLatin1String("patch"))
        return QStringLiteral("pencil");
    if (name == QLatin1String("bash") || name == QLatin1String("shell"))
        return QStringLiteral("terminal");
    if (name == QLatin1String("list") || name == QLatin1String("glob"))
        return QStringLiteral("folder");
    if (name == QLatin1String("grep") || name == QLatin1String("websearch"))
        return QStringLiteral("search");
    if (name == QLatin1String("skill"))
        return QStringLiteral("sparkle");
    if (name == QLatin1String("webfetch"))
        return QStringLiteral("globe");
    if (name.startsWith(QLatin1String("todo")))
        return QStringLiteral("list");
    return QStringLiteral("blocks");
}

QString summarize(const QJsonObject &input)
{
    for (const char *key : {"command", "path", "filePath", "pattern", "name", "url", "query", "description"}) {
        const QString v = input.value(QLatin1String(key)).toString();
        if (!v.isEmpty())
            return v.simplified();
    }
    return input.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
}

}

ChatModel::ChatModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Streaming deltas arrive token by token; re-split markdown at most every 50 ms.
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(50);
    connect(&m_throttle, &QTimer::timeout, this, &ChatModel::flushDirty);
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
    return {{KindRole, "kind"},           {TextRole, "text"},
            {SegmentsRole, "segments"},   {SkillsRole, "skills"},
            {ToolNameRole, "toolName"},   {ToolTitleRole, "toolTitle"},
            {ToolSubtitleRole, "toolSubtitle"}, {ToolStatusRole, "toolStatus"},
            {ToolDetailsRole, "toolDetails"},   {ToolIconRole, "toolIcon"},
            {LastInTurnRole, "lastInTurn"}, {DetailRole, "detail"}};
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &it = m_items.at(index.row());
    switch (role) {
    case KindRole: return it.kind;
    case TextRole: return it.text;
    case DetailRole: return it.detail;
    case SegmentsRole: return it.segments;
    case SkillsRole: return it.skills;
    case ToolNameRole: return it.toolName;
    case ToolTitleRole: return toolTitle(it.toolName);
    case ToolSubtitleRole: return it.toolSubtitle;
    case ToolStatusRole: return it.toolStatus;
    case ToolIconRole: return toolIcon(it.toolName);
    case ToolDetailsRole: {
        QString d = it.toolInput;
        if (!it.toolOutput.isEmpty())
            d += (d.isEmpty() ? QString() : QStringLiteral("\n\n")) + it.toolOutput;
        return d;
    }
    case LastInTurnRole: {
        // The last assistant text row before the next user message gets the action bar.
        if (it.kind != QLatin1String("text"))
            return false;
        for (int i = index.row() + 1; i < m_items.size(); ++i) {
            if (m_items.at(i).kind == QLatin1String("user"))
                return true;
            if (m_items.at(i).kind == QLatin1String("text"))
                return false;
        }
        return true;
    }
    default: return {};
    }
}

// Turns bare http(s):// and www. addresses into markdown autolinks so they are
// clickable; leaves inline code, existing [links](…) and <autolinks> alone.
static QString linkifyMarkdown(const QString &prose)
{
    static const QRegularExpression url(
        QStringLiteral("(?<![\\w/<(\\[\"'=])((?:https?://|www\\.)[^\\s<>()\\[\\]`\"']*[^\\s<>()\\[\\]`\"'.,;:!?*_])"),
        QRegularExpression::CaseInsensitiveOption);
    QString out;
    const QStringList parts = prose.split(QLatin1Char('`')); // odd parts are inline code
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0)
            out += QLatin1Char('`');
        if (i % 2 == 1) {
            out += parts.at(i);
            continue;
        }
        QString part = parts.at(i);
        QString result;
        qsizetype last = 0;
        auto it = url.globalMatch(part);
        while (it.hasNext()) {
            const auto m = it.next();
            // Skip URLs that already sit inside a markdown link target "](…)".
            if (m.capturedStart() >= 2 && part.mid(m.capturedStart() - 2, 2) == QLatin1String("]("))
                continue;
            result += part.mid(last, m.capturedStart() - last);
            const QString target = m.captured(1).startsWith(QStringLiteral("www."), Qt::CaseInsensitive)
                                       ? QStringLiteral("https://") + m.captured(1) : m.captured(1);
            result += QStringLiteral("[%1](%2)").arg(m.captured(1), target);
            last = m.capturedEnd();
        }
        result += part.mid(last);
        out += result;
    }
    return out;
}

QVariantList ChatModel::splitMarkdown(const QString &markdown)
{
    // Separate fenced code blocks (``` or ~~~) from prose; an unterminated
    // fence while streaming still renders as a code block.
    QVariantList out;
    QString prose;
    QString code;
    QString lang;
    QString fence;
    const auto flushProse = [&] {
        if (!prose.trimmed().isEmpty())
            out << QVariantMap{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("text"), linkifyMarkdown(prose.trimmed())}};
        prose.clear();
    };
    static const QRegularExpression open(QStringLiteral("^\\s{0,3}(```+|~~~+)\\s*([^`\\s]*)"));
    for (const QString &line : markdown.split(QLatin1Char('\n'))) {
        if (fence.isEmpty()) {
            const auto m = open.match(line);
            if (m.hasMatch()) {
                flushProse();
                fence = m.captured(1);
                lang = m.captured(2);
                code.clear();
            } else {
                prose += line + QLatin1Char('\n');
            }
        } else if (line.trimmed().startsWith(fence) && line.trimmed().remove(fence.at(0)).isEmpty()) {
            if (code.endsWith(QLatin1Char('\n')))
                code.chop(1);
            out << QVariantMap{{QStringLiteral("type"), QStringLiteral("code")}, {QStringLiteral("text"), code},
                               {QStringLiteral("lang"), lang}};
            fence.clear();
        } else {
            code += line + QLatin1Char('\n');
        }
    }
    if (!fence.isEmpty()) {
        if (code.endsWith(QLatin1Char('\n')))
            code.chop(1);
        out << QVariantMap{{QStringLiteral("type"), QStringLiteral("code")}, {QStringLiteral("text"), code},
                           {QStringLiteral("lang"), lang}};
    }
    flushProse();
    return out;
}

int ChatModel::indexOfKey(const QString &key) const
{
    for (int i = int(m_items.size()) - 1; i >= 0; --i)
        if (m_items.at(i).key == key)
            return i;
    return -1;
}

int ChatModel::append(const Item &item)
{
    const int row = int(m_items.size());
    beginInsertRows({}, row, row);
    m_items << item;
    endInsertRows();
    if (row > 0) // previous row may have lost "lastInTurn"
        Q_EMIT dataChanged(index(row - 1), index(row - 1), {LastInTurnRole});
    Q_EMIT countChanged();
    return row;
}

void ChatModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_dirty.clear();
    endResetModel();
    Q_EMIT countChanged();
}

void ChatModel::addUser(const QString &text, const QStringList &skills)
{
    Item it;
    it.kind = QStringLiteral("user");
    it.text = text;
    it.skills = skills;
    append(it);
}

void ChatModel::appendText(const QString &key, const QString &delta)
{
    int row = indexOfKey(key);
    if (row < 0) {
        Item it;
        it.kind = QStringLiteral("text");
        it.key = key;
        row = append(it);
    }
    m_items[row].text += delta;
    m_dirty.insert(key);
    if (!m_throttle.isActive())
        m_throttle.start();
}

void ChatModel::flushDirty()
{
    for (const QString &key : std::as_const(m_dirty)) {
        const int row = indexOfKey(key);
        if (row < 0)
            continue;
        m_items[row].segments = splitMarkdown(m_items.at(row).text);
        Q_EMIT dataChanged(index(row), index(row), {TextRole, SegmentsRole});
    }
    m_dirty.clear();
}

void ChatModel::setText(const QString &key, const QString &text)
{
    int row = indexOfKey(key);
    if (row < 0) {
        if (text.trimmed().isEmpty())
            return;
        Item it;
        it.kind = QStringLiteral("text");
        it.key = key;
        row = append(it);
    }
    if (text.trimmed().isEmpty()) {
        // e.g. a local model's JSON tool call that was turned into a real call
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
        Q_EMIT countChanged();
        return;
    }
    m_items[row].text = text;
    m_items[row].segments = splitMarkdown(text);
    m_dirty.remove(key);
    Q_EMIT dataChanged(index(row), index(row), {TextRole, SegmentsRole});
}

void ChatModel::appendReasoning(const QString &key, const QString &delta)
{
    const QString k = QStringLiteral("r:") + key;
    int row = indexOfKey(k);
    if (row < 0) {
        Item it;
        it.kind = QStringLiteral("reasoning");
        it.key = k;
        row = append(it);
    }
    m_items[row].text += delta;
    Q_EMIT dataChanged(index(row), index(row), {TextRole});
}

void ChatModel::toolUpdate(const QString &id, const QString &name, const QJsonObject &input, const QString &status,
                           const QString &output)
{
    const QString k = QStringLiteral("tool:") + id;
    int row = indexOfKey(k);
    if (row < 0) {
        Item it;
        it.kind = QStringLiteral("tool");
        it.key = k;
        it.toolName = name;
        row = append(it);
    }
    Item &it = m_items[row];
    if (!name.isEmpty())
        it.toolName = name;
    if (!input.isEmpty()) {
        it.toolSubtitle = summarize(input);
        const QString cmd = input.value(QStringLiteral("command")).toString();
        it.toolInput = cmd.isEmpty() ? QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Indented)).trimmed()
                                     : QStringLiteral("$ ") + cmd;
    }
    if (!output.isEmpty())
        it.toolOutput = output.left(20000);
    it.toolStatus = status.isEmpty() ? QStringLiteral("running") : status;
    Q_EMIT dataChanged(index(row), index(row));
}

// "17h41m39.7s", "45s", "1m20s" -> "17 sa 41 dk" / "45 sn" / "1 dk 20 sn".
static QString retryText(const QString &l)
{
    static const QRegularExpression in(QStringLiteral(
        R"((?:retry in|try again in|retrydelay\W*)\s*((?:\d+(?:\.\d+)?\s*(?:ms|h|m|s)\s*)+))"));
    static const QRegularExpression after(QStringLiteral(R"(retry[ _-]?after\D{0,4}(\d+))"));
    static const QRegularExpression part(QStringLiteral(R"((\d+(?:\.\d+)?)\s*(ms|h|m|s))"));
    qint64 secs = 0;
    if (const auto m = in.match(l); m.hasMatch()) {
        for (auto it = part.globalMatch(m.captured(1)); it.hasNext();) {
            const auto p = it.next();
            const double v = p.captured(1).toDouble();
            const QString u = p.captured(2);
            secs += qint64(u == QLatin1String("h") ? v * 3600 : u == QLatin1String("m") ? v * 60
                           : u == QLatin1String("s") ? v : v / 1000);
        }
    } else if (const auto m = after.match(l); m.hasMatch()) {
        secs = m.captured(1).toLongLong();
    }
    if (secs <= 0)
        return {};
    const qint64 h = secs / 3600, m = secs % 3600 / 60, s = secs % 60;
    if (h > 0)
        return m > 0 ? QStringLiteral("%1 sa %2 dk").arg(h).arg(m) : QStringLiteral("%1 sa").arg(h);
    if (m > 0)
        return s > 0 && m < 5 ? QStringLiteral("%1 dk %2 sn").arg(m).arg(s) : QStringLiteral("%1 dk").arg(m);
    return QStringLiteral("%1 sn").arg(qMax<qint64>(s, 1));
}

static QString providerName(const QString &l)
{
    if (l.contains(QLatin1String("googleapis")) || l.contains(QLatin1String("aistudio")) || l.contains(QLatin1String("gemini")))
        return QStringLiteral("Google");
    if (l.contains(QLatin1String("openrouter")))
        return QStringLiteral("OpenRouter");
    if (l.contains(QLatin1String("anthropic")) || l.contains(QLatin1String("claude")))
        return QStringLiteral("Anthropic");
    if (l.contains(QLatin1String("openai")))
        return QStringLiteral("OpenAI");
    if (l.contains(QLatin1String("deepseek")))
        return QStringLiteral("DeepSeek");
    if (l.contains(QLatin1String("groq")))
        return QStringLiteral("Groq");
    if (l.contains(QLatin1String("mistral")))
        return QStringLiteral("Mistral");
    if (l.contains(QLatin1String("x.ai")) || l.contains(QLatin1String("grok")))
        return QStringLiteral("xAI");
    return {};
}

QString ChatModel::friendlyError(const QString &raw, const QString &modelName)
{
    const QString l = raw.toLower();
    const QString provider = providerName(l);
    const QString who = provider.isEmpty() ? QCoreApplication::translate("Orbital", "Sağlayıcı") : provider;
    QString model = modelName;
    if (model.isEmpty()) {
        static const QRegularExpression m(QStringLiteral(R"(model:\s*([\w.\-/:]+))"));
        model = m.match(raw).captured(1);
    }
    const QString subject = model.isEmpty() ? QStringLiteral("Bu model") : model;
    const QString wait = retryText(l);
    const QString later = wait.isEmpty() ? QString() : QCoreApplication::translate("Orbital", " Yaklaşık %1 sonra yeniden kullanılabilir.").arg(wait);
    const auto has = [&l](std::initializer_list<const char *> keys) {
        for (const char *k : keys)
            if (l.contains(QLatin1String(k)))
                return true;
        return false;
    };
    QString title, hint;

    if (has({"limit: 0", "limit:0"}) && has({"free_tier", "free tier"})) {
        title = QCoreApplication::translate("Orbital", "%1 ücretsiz katmanda kullanılamıyor.").arg(subject);
        hint = QCoreApplication::translate("Orbital", "%1 bu model için ücretsiz kota vermiyor. Hesabında faturalandırmayı aç ya da başka bir model seç.").arg(who);
    } else if (has({"credit balance", "insufficient_quota", "exceeded your current quota", "billing", "payment required",
                    "insufficient balance", "insufficient credits", "402"})) {
        title = QCoreApplication::translate("Orbital", "%1 hesabının kotası ya da bakiyesi bitti.").arg(who);
        hint = QCoreApplication::translate("Orbital", "Plan ve fatura ayarlarını kontrol et ya da başka bir model seç.") + later;
    } else if (!has({"per day", "daily"}) && has({"rate limit", "rate_limit", "too many requests", "resource_exhausted", "resourceexhausted", "429"})) {
        title = QCoreApplication::translate("Orbital", "Çok hızlı istek gönderildi.");
        hint = QCoreApplication::translate("Orbital", "%1 kısa süreli bir sınıra takıldı; biraz bekleyip tekrar dene.").arg(who) + later;
    } else if (has({"per day", "daily", "free usage", "usage limit", "limit reached", "quota", "resource_exhausted", "resourceexhausted", "429"})) {
        title = QCoreApplication::translate("Orbital", "%1 kullanım sınırına ulaştı.").arg(subject);
        hint = QCoreApplication::translate("Orbital", "Başka bir model seçebilirsin.") + later;
    } else if (has({"overloaded", "503", "service unavailable", "temporarily unavailable", "capacity"})) {
        title = QCoreApplication::translate("Orbital", "%1 şu an çok yoğun.").arg(who);
        hint = QCoreApplication::translate("Orbital", "Birazdan tekrar dene ya da başka bir model seç.");
    } else if (has({"invalid api key", "incorrect api key", "api key not valid", "invalid_api_key", "invalid x-api-key",
                    "unauthorized", "401", "authentication"})) {
        title = QCoreApplication::translate("Orbital", "%1 API anahtarı geçersiz.").arg(who);
        hint = QCoreApplication::translate("Orbital", "Ayarlar → Sağlayıcılar'dan anahtarı kontrol edip yeniden bağla.");
    } else if (has({"permission denied", "permission_denied", "403", "forbidden", "not allowed"})) {
        title = QCoreApplication::translate("Orbital", "%1 bu isteğe izin vermedi.").arg(who);
        hint = QCoreApplication::translate("Orbital", "Anahtarının bu modele erişimi olmayabilir; başka bir model seç ya da hesabını kontrol et.");
    } else if (has({"context length", "context_length", "maximum context", "too many tokens", "prompt is too long",
                    "context window", "maximum number of tokens"})) {
        title = QCoreApplication::translate("Orbital", "Sohbet modelin bağlam sınırını aştı.");
        hint = QCoreApplication::translate("Orbital", "Yeni bir sohbet başlat ya da daha büyük bağlamlı bir model seç.");
    } else if (has({"model not found", "model_not_found", "does not exist", "not supported", "no longer available",
                    "unknown model", "404"})) {
        title = QCoreApplication::translate("Orbital", "%1 bulunamadı ya da artık sunulmuyor.").arg(subject);
        hint = QCoreApplication::translate("Orbital", "Başka bir model seç.");
    } else if (has({"safety", "content_filter", "content filter", "blocked", "moderation"})) {
        title = QCoreApplication::translate("Orbital", "%1 bu isteği güvenlik filtresi nedeniyle yanıtlamadı.").arg(who);
        hint = QCoreApplication::translate("Orbital", "İsteği farklı ifade etmeyi dene.");
    } else if (has({"timed out", "timeout", "connection refused", "could not resolve", "network", "econnreset",
                    "host not found", "ssl"})) {
        title = QCoreApplication::translate("Orbital", "Sağlayıcıya ulaşılamadı.");
        hint = QCoreApplication::translate("Orbital", "İnternet bağlantını kontrol edip tekrar dene.");
    } else {
        return {};
    }
    return title + QLatin1Char('\n') + hint;
}

void ChatModel::addError(const QString &text, const QString &modelName)
{
    Item it;
    it.kind = QStringLiteral("error");
    const QString friendly = friendlyError(text, modelName);
    it.text = friendly.isEmpty() ? text : friendly;
    it.detail = friendly.isEmpty() ? QString() : text;
    append(it);
}

void ChatModel::addHistory(const QJsonArray &items)
{
    beginResetModel();
    m_items.clear();
    int n = 0;
    for (const QJsonValue &v : items) {
        const QJsonObject item = v.toObject();
        if (item.value(QStringLiteral("role")).toString() == QLatin1String("user")) {
            Item it;
            it.kind = QStringLiteral("user");
            it.text = item.value(QStringLiteral("text")).toString();
            for (const QJsonValue &s : item.value(QStringLiteral("skills")).toArray())
                it.skills << s.toString();
            m_items << it;
            continue;
        }
        for (const QJsonValue &pv : item.value(QStringLiteral("parts")).toArray()) {
            const QJsonObject part = pv.toObject();
            const QString kind = part.value(QStringLiteral("type")).toString();
            const QString text = part.value(QStringLiteral("text")).toString();
            Item it;
            it.key = QStringLiteral("h%1").arg(n++);
            if (kind == QLatin1String("text") && !text.trimmed().isEmpty()) {
                it.kind = QStringLiteral("text");
                it.text = text;
                it.segments = splitMarkdown(text);
            } else if (kind == QLatin1String("reasoning") && !text.trimmed().isEmpty()) {
                it.kind = QStringLiteral("reasoning");
                it.text = text;
            } else if (kind == QLatin1String("tool")) {
                it.kind = QStringLiteral("tool");
                it.toolName = part.value(QStringLiteral("name")).toString();
                const QJsonObject input = part.value(QStringLiteral("input")).toObject();
                it.toolSubtitle = summarize(input);
                const QString cmd = input.value(QStringLiteral("command")).toString();
                it.toolInput = cmd.isEmpty() ? QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Indented)).trimmed()
                                             : QStringLiteral("$ ") + cmd;
                it.toolOutput = part.value(QStringLiteral("output")).toString().left(20000);
                it.toolStatus = part.value(QStringLiteral("status")).toString();
            } else {
                continue;
            }
            m_items << it;
        }
        const QString error = item.value(QStringLiteral("error")).toString();
        if (!error.isEmpty()) {
            Item it;
            it.kind = QStringLiteral("error");
            const QString friendly = friendlyError(error);
            it.text = friendly.isEmpty() ? error : friendly;
            it.detail = friendly.isEmpty() ? QString() : error;
            m_items << it;
        }
    }
    endResetModel();
    Q_EMIT countChanged();
}

QString ChatModel::turnText(int row) const
{
    QStringList parts;
    for (int i = row; i >= 0 && m_items.at(i).kind != QLatin1String("user"); --i)
        if (m_items.at(i).kind == QLatin1String("text"))
            parts.prepend(m_items.at(i).text.trimmed());
    return parts.join(QStringLiteral("\n\n"));
}

QString ChatModel::lastHtmlBlock() const
{
    static const QRegularExpression fence(QStringLiteral("```([A-Za-z0-9_+-]*)[^\\n]*\\n([\\s\\S]*?)```"));
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).kind != QLatin1String("text"))
            continue;
        QString html, loose;
        auto it = fence.globalMatch(m_items.at(i).text);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString lang = m.captured(1).toLower();
            const QString body = m.captured(2).trimmed();
            if (lang == QLatin1String("html") || lang == QLatin1String("htm"))
                html = body;  // aynı cevapta birden çok blok varsa sonuncusu
            else if (body.contains(QLatin1String("<html"), Qt::CaseInsensitive)
                     || body.contains(QLatin1String("<body"), Qt::CaseInsensitive))
                loose = body;
        }
        if (!html.isEmpty())
            return html;
        if (!loose.isEmpty())
            return loose;
    }
    return {};
}

QString ChatModel::lastUserPrompt() const
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).kind == QLatin1String("user"))
            return m_items.at(i).text;
    }
    return {};
}

QString ChatModel::exportMarkdown(const QString &sessionTitle, const QString &projectName) const
{
    QString out = QStringLiteral("# %1\n\n_Proje: %2_\n\n---\n\n")
                      .arg(sessionTitle.isEmpty() ? QCoreApplication::translate("Orbital", "Orbital Sohbeti") : sessionTitle,
                           projectName.isEmpty() ? QCoreApplication::translate("Orbital", "Proje") : projectName);
    for (const Item &it : m_items) {
        if (it.kind == QLatin1String("user")) {
            out += QCoreApplication::translate("Orbital", "### 👤 Kullanıcı\n\n%1\n\n").arg(it.text);
        } else if (it.kind == QLatin1String("text")) {
            out += QStringLiteral("### 🤖 Orbital\n\n%1\n\n").arg(it.text);
        } else if (it.kind == QLatin1String("tool")) {
            out += QCoreApplication::translate("Orbital", "> ⚙️ **Araç:** `%1` (%2)\n\n").arg(it.toolName, it.toolStatus);
        } else if (it.kind == QLatin1String("error")) {
            out += QStringLiteral("> ⚠️ **Hata:** %1\n\n").arg(it.text);
        }
    }
    return out;
}

// ---------------------------------------------------------------- sessions

QHash<int, QByteArray> SessionModel::roleNames() const
{
    return {{IdRole, "sessionId"},
            {TitleRole, "title"},
            {SectionRole, "section"},
            {TimeRole, "time"},
            {DirectoryRole, "directory"},
            {ProjectNameRole, "projectName"}};
}

QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case TitleRole:
        // The engine names untitled sessions "New session - <timestamp>".
        return r.title.isEmpty() || r.title.startsWith(QStringLiteral("New session - ")) ? QCoreApplication::translate("Orbital", "Yeni sohbet") : r.title;
    case TimeRole: return QDateTime::fromMSecsSinceEpoch(r.updated).toString(QStringLiteral("dd.MM.yyyy HH:mm"));
    case DirectoryRole: return r.directory;
    case ProjectNameRole: {
        if (r.directory.isEmpty() || r.directory == Paths::noFolderDir())
            return QCoreApplication::translate("Orbital", "Klasörsüz (no-folder)");
        const QString name = QDir(r.directory).dirName();
        return name.isEmpty() ? r.directory : name;
    }
    case SectionRole: {
        const QDate d = QDateTime::fromMSecsSinceEpoch(r.updated).date();
        const QDate today = QDate::currentDate();
        if (d >= today)
            return QCoreApplication::translate("Orbital", "Bugün");
        if (d == today.addDays(-1))
            return QCoreApplication::translate("Orbital", "Dün");
        if (d > today.addDays(-7))
            return QCoreApplication::translate("Orbital", "Önceki 7 gün");
        if (d > today.addDays(-30))
            return QCoreApplication::translate("Orbital", "Önceki 30 gün");
        return QCoreApplication::translate("Orbital", "Daha eski");
    }
    default: return {};
    }
}

void SessionModel::setRows(const QList<Row> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

QString SessionModel::titleOf(const QString &id) const
{
    for (const Row &r : m_rows)
        if (r.id == id)
            return r.title;
    return {};
}

QString SessionModel::directoryOf(const QString &id) const
{
    for (const Row &r : m_rows)
        if (r.id == id)
            return r.directory;
    return {};
}
