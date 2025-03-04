#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QList>
#include <QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QSslConfiguration>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>

// Structure for location info.
struct Location {
    QString filename;
    int line;
};

// Structure for holding message information.
struct MessageInfo {
    QList<Location> locations;
    QString source;
    QString translation;
    QString translationType; // e.g., "unfinished"
};

// Parse the TS file into a mapping from context names to lists of messages.
QMap<QString, QList<MessageInfo>> parseTsFile(const QString &filePath)
{
    QMap<QString, QList<MessageInfo>> contextMap;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Unable to open file:" << filePath;
        return contextMap;
    }
    QXmlStreamReader xml(&file);
    QString contextName;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("context")) {
                contextName.clear();
                QList<MessageInfo> messages;
                while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QLatin1String("context"))) {
                    if (xml.tokenType() == QXmlStreamReader::StartElement) {
                        if (xml.name() == QLatin1String("name")) {
                            contextName = xml.readElementText();
                        } else if (xml.name() == QLatin1String("message")) {
                            MessageInfo msg;
                            while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QLatin1String("message"))) {
                                if (xml.tokenType() == QXmlStreamReader::StartElement) {
                                    if (xml.name() == QLatin1String("location")) {
                                        QXmlStreamAttributes attrs = xml.attributes();
                                        Location loc;
                                        loc.filename = attrs.value("filename").toString();
                                        loc.line = attrs.value("line").toInt();
                                        msg.locations.append(loc);
                                        xml.skipCurrentElement();
                                    } else if (xml.name() == QLatin1String("source")) {
                                        msg.source = xml.readElementText();
                                    } else if (xml.name() == QLatin1String("translation")) {
                                        QXmlStreamAttributes attrs = xml.attributes();
                                        msg.translationType = attrs.value("type").toString();
                                        msg.translation = xml.readElementText();
                                    }
                                }
                                xml.readNext();
                            }
                            messages.append(msg);
                        }
                    }
                    xml.readNext();
                }
                if (!contextName.isEmpty())
                    contextMap.insert(contextName, messages);
            }
        }
    }
    if (xml.hasError())
        qWarning() << "XML Parsing Error:" << xml.errorString();
    file.close();
    return contextMap;
}

// Write the updated TS file with the new translations.
bool writeTsFile(const QString &filePath, const QMap<QString, QList<MessageInfo>> &translations)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Unable to open file for writing:" << filePath;
        return false;
    }
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeDTD("<!DOCTYPE TS>");
    writer.writeStartElement("TS");
    writer.writeAttribute("version", "2.1");
    writer.writeAttribute("language", "en_US"); // Adjust if needed.

    // Iterate over contexts.
    for (auto contextIt = translations.constBegin(); contextIt != translations.constEnd(); ++contextIt) {
        writer.writeStartElement("context");
        writer.writeTextElement("name", contextIt.key());
        // Iterate over messages.
        for (const MessageInfo &msg : contextIt.value()) {
            writer.writeStartElement("message");
            // Write each location.
            for (const Location &loc : msg.locations) {
                writer.writeEmptyElement("location");
                writer.writeAttribute("filename", loc.filename);
                writer.writeAttribute("line", QString::number(loc.line));
            }
            writer.writeTextElement("source", msg.source);
            writer.writeStartElement("translation");
            // Set type attribute if translation is still empty.
            if (msg.translation.isEmpty())
                writer.writeAttribute("type", "unfinished");
            writer.writeCharacters(msg.translation);
            writer.writeEndElement(); // </translation>
            writer.writeEndElement(); // </message>
        }
        writer.writeEndElement(); // </context>
    }
    writer.writeEndElement(); // </TS>
    writer.writeEndDocument();
    file.close();
    return true;
}

// Read API key from file (assumes the file contains the key as plain text)
QString readApiKeyFromFile(const QString &apiKeyPath)
{
    QFile keyFile(apiKeyPath);
    if (!keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Unable to open API key file:" << apiKeyPath;
        return QString();
    }

    QByteArray raw = keyFile.readAll();
    if (raw.startsWith("\xEF\xBB\xBF")) {
        raw.remove(0, 3);
    }
    QString key = QString::fromUtf8(raw).trimmed();
    keyFile.close();
    return key;
}

// Send a batch of phrases to the GPT API and return the raw response data.
QByteArray sendTranslationBatch(const QStringList &phrases, const QString &apiKey,
                                const QString &lang, const QString &langPostfix)
{
    // Build the prompt by listing the phrases (each on a new line).
    QString prompt = QString("Translate the following phrases into %1 (%2). Return only a JSON array of objects "
                             "in the format [{\"source\": \"<original>\", \"translation\": \"<translated>\"}].\nPhrases:\n%3")
                         .arg(lang).arg(langPostfix)
                         .arg(phrases.join("\n"));

    QJsonObject requestBody;
    requestBody["model"] = "gpt-4o-mini";

    QJsonArray messages;
    {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = "You are a translation assistant.";
        messages.append(systemMsg);
    }
    {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = prompt;
        messages.append(userMsg);
    }
    requestBody["messages"] = messages;
    requestBody["temperature"] = 0;

    QJsonDocument jsonDoc(requestBody);
    QByteArray postData = jsonDoc.toJson();

    QNetworkRequest request(QUrl("https://api.openai.com/v1/chat/completions"));
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());
    request.setRawHeader("User-Agent", "QtGPTTranslator/1.0");

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    request.setSslConfiguration(sslConfig);

    qDebug() << "Sending request";
    QNetworkAccessManager networkManager;
    QNetworkReply *reply = networkManager.post(request, postData);

    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Network error:" << reply->errorString();
        reply->deleteLater();
        return QByteArray();
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();
    return responseData;
}

// Helper to process an API response and update the translations.
void processResponse(const QByteArray &responseData, QMap<QString, QList<MessageInfo>> &translations)
{
    if (responseData.isEmpty())
        return;

    QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
    if (responseDoc.isNull() || !responseDoc.isObject()) {
        qWarning() << "Failed to parse API response as JSON.";
        return;
    }
    QJsonObject responseObj = responseDoc.object();
    QJsonArray choices = responseObj["choices"].toArray();
    if (choices.isEmpty()) {
        qWarning() << "No choices returned from API.";
        return;
    }
    QJsonObject firstChoice = choices.first().toObject();
    QJsonObject messageObj = firstChoice["message"].toObject();
    QString content = messageObj["content"].toString();

    // Remove code block markers if present.
    QRegularExpression codeBlockRegex("```(?:json)?\\s*([\\s\\S]*?)\\s*```");
    QRegularExpressionMatch match = codeBlockRegex.match(content);
    if (match.hasMatch())
        content = match.captured(1);

    QJsonDocument parsedContent = QJsonDocument::fromJson(content.toUtf8());
    if (parsedContent.isNull() || !parsedContent.isArray()) {
        qWarning() << "Failed to parse the returned translation JSON.";
        return;
    }
    QJsonArray translationsArray = parsedContent.array();

    // Build a mapping from source to translation.
    QMap<QString, QString> translationMapping;
    for (const QJsonValue &val : translationsArray) {
        if (val.isObject()) {
            QJsonObject obj = val.toObject();
            QString source = obj["source"].toString();
            QString translation = obj["translation"].toString();
            if (!source.isEmpty())
                translationMapping.insert(source, translation);
        }
    }

    // Update the context map with the translations.
    for (auto &messages : translations) {
        for (auto &msg : messages) {
            if (translationMapping.contains(msg.source))
                msg.translation = translationMapping.value(msg.source);
        }
    }
}

//---------------------------------------------------------------------
// Main function
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("Qt GPT Translator");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Translate TS files using a GPT API");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption tsFilePathOption(QStringList() << "t" << "ts_file_path",
                                        "Path to the TS file.",
                                        "ts_file_path");
    QCommandLineOption apiKeyPathOption(QStringList() << "k" << "api_key_path",
                                        "Path to the GPT API key file.",
                                        "api_key_path");
    QCommandLineOption apiCallSizeOption(QStringList() << "s" << "api_call_size",
                                         "Number of source elements per API call.",
                                         "api_call_size",
                                         "50");
    QCommandLineOption langOption("lang",
                                  "Target language for translation.",
                                  "lang");
    QCommandLineOption langPostfixOption("lang_postfix",
                                         "Language postfix (e.g., en_EN, tr_TR, ru_RU).",
                                         "lang_postfix");

    parser.addOption(tsFilePathOption);
    parser.addOption(apiKeyPathOption);
    parser.addOption(apiCallSizeOption);
    parser.addOption(langOption);
    parser.addOption(langPostfixOption);

    parser.process(app);

    if (!parser.isSet(tsFilePathOption) ||
        !parser.isSet(apiKeyPathOption) ||
        !parser.isSet(langOption) ||
        !parser.isSet(langPostfixOption)) {
        qCritical() << "Error: Missing required options.";
        parser.showHelp(1);
    }

    QString tsFilePath = parser.value(tsFilePathOption);
    QString apiKeyPath = parser.value(apiKeyPathOption);
    bool ok = false;
    int apiCallSize = parser.value(apiCallSizeOption).toInt(&ok);
    if (!ok) {
        qCritical() << "Error: api_call_size must be an integer.";
        return 1;
    }
    QString lang = parser.value(langOption);
    QString langPostfix = parser.value(langPostfixOption);

    qDebug() << "TS File Path:" << tsFilePath;
    qDebug() << "API Key Path:" << apiKeyPath;
    qDebug() << "API Call Size:" << apiCallSize;
    qDebug() << "Language:" << lang;
    qDebug() << "Language Postfix:" << langPostfix;

    // Read the API key.
    QString apiKey = readApiKeyFromFile(apiKeyPath);
    if (apiKey.isEmpty()) {
        qCritical() << "API key is empty or could not be read.";
        return 1;
    }

    // Parse the TS file.
    QMap<QString, QList<MessageInfo>> translations = parseTsFile(tsFilePath);

    // Batch processing: accumulate untranslated source phrases.
    QStringList batchPhrases;

    // Iterate over each context and its messages.
    for (auto contextIt = translations.begin(); contextIt != translations.end(); ++contextIt) {
        QList<MessageInfo> &messages = contextIt.value();
        for (MessageInfo &msg : messages) {
            if (!msg.source.isEmpty() && msg.translation.isEmpty()) {
                batchPhrases.append(msg.source);
                if (batchPhrases.size() >= apiCallSize) {
                    QByteArray responseData = sendTranslationBatch(batchPhrases, apiKey , lang, langPostfix);
                    processResponse(responseData, translations);
                    batchPhrases.clear();
                }
            }
        }
    }

    // Process any remaining phrases.
    if (!batchPhrases.isEmpty()) {
        QByteArray responseData = sendTranslationBatch(batchPhrases, apiKey, lang, langPostfix);
        processResponse(responseData, translations);
        batchPhrases.clear();
    }

    // Write the updated translations back to the TS file.
    if (!writeTsFile(tsFilePath, translations)) {
        qCritical() << "Failed to write back to TS file.";
        return 1;
    }

    return 0;
}
