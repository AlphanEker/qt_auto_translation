#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>
#include <QMap>
#include <QString>
#include <QStringList>

// For network and JSON
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QSslConfiguration>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QRegularExpression>

//---------------------------------------------------------------------
// Parse the TS file into a nested QMap: context -> (source -> translation)
QMap<QString, QMap<QString, QString>> parseTsFile(const QString &filePath)
{
    QMap<QString, QMap<QString, QString>> contextMap;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Unable to open file:" << filePath;
        return contextMap;
    }

    QXmlStreamReader xml(&file);
    QString currentContext;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("context")) {
                currentContext.clear();
                QMap<QString, QString> messages;
                while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QLatin1String("context"))) {
                    if (xml.tokenType() == QXmlStreamReader::StartElement) {
                        if (xml.name() == QLatin1String("name")) {
                            currentContext = xml.readElementText();
                        } else if (xml.name() == QLatin1String("message")) {
                            QString sourceText;
                            QString translationText;
                            while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QLatin1String("message"))) {
                                if (xml.tokenType() == QXmlStreamReader::StartElement) {
                                    if (xml.name() == QLatin1String("source")) {
                                        sourceText = xml.readElementText();
                                    } else if (xml.name() == QLatin1String("translation")) {
                                        translationText = xml.readElementText();
                                    }
                                }
                                xml.readNext();
                            }
                            if (!sourceText.isEmpty())
                                messages.insert(sourceText, translationText);
                        }
                    }
                    xml.readNext();
                }
                if (!currentContext.isEmpty())
                    contextMap.insert(currentContext, messages);
            }
        }
    }

    if (xml.hasError())
        qWarning() << "XML Parsing Error:" << xml.errorString();

    file.close();
    return contextMap;
}

//---------------------------------------------------------------------
// Read API key from file (assumes the file contains the key as plain text)
QString readApiKeyFromFile(const QString &apiKeyPath)
{
    QFile keyFile(apiKeyPath);
    if (!keyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Unable to open API key file:" << apiKeyPath;
        return QString();
    }
    QString key = keyFile.readAll().trimmed();
    keyFile.close();
    return key;
}

//---------------------------------------------------------------------
// Send a batch of phrases to the GPT API and return a mapping of source -> translation.
// The GPT API request is synchronous for simplicity.
QMap<QString, QString> sendTranslationBatch(const QStringList &phrases, const QString &apiKey,
                                            const QString &lang, const QString &langPostfix)
{
    QMap<QString, QString> result;

    // Build the prompt by listing the phrases (each on a new line)
    QString prompt = QString("Translate the following phrases into %1 (%2). Return only a JSON array of objects "
                             "in the format [{\"source\": \"<original>\", \"translation\": \"<translated>\"}].\nPhrases:\n%3")
                         .arg(lang).arg(langPostfix)
                         .arg(phrases.join("\n"));

    // Construct the JSON body for the GPT API request.
    QJsonObject requestBody;
    // Try either model name as needed:
    requestBody["model"] = "gpt-4o-mini-2024-07-18";  // or "gpt-4o-mini" if that proves to be correct

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

    // Prepare the network request.
    QNetworkRequest request(QUrl("https://api.openai.com/v1/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());
    request.setRawHeader("User-Agent", "QtGPTTranslator/1.0");

    // Force TLS 1.2+ to avoid handshake issues.
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    request.setSslConfiguration(sslConfig);

    // Log the request for debugging.
    qDebug() << "Sending request with payload:" << postData;

    // Send the POST request.
    QNetworkAccessManager networkManager;
    QNetworkReply *reply = networkManager.post(request, postData);

    // Synchronously wait for the reply.
    QEventLoop eventLoop;
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();

    // Process the reply.
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Network error:" << reply->errorString();
        reply->deleteLater();
        return result;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    // Parse the API response.
    QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
    if (responseDoc.isNull() || !responseDoc.isObject()) {
        qWarning() << "Failed to parse API response as JSON.";
        return result;
    }

    QJsonObject responseObj = responseDoc.object();
    QJsonArray choices = responseObj["choices"].toArray();
    if (choices.isEmpty()) {
        qWarning() << "No choices returned from API.";
        return result;
    }

    QJsonObject firstChoice = choices.first().toObject();
    QJsonObject messageObj = firstChoice["message"].toObject();
    QString content = messageObj["content"].toString();

    // Parse the returned content as JSON.
    QJsonDocument parsedContent = QJsonDocument::fromJson(content.toUtf8());
    if (parsedContent.isNull() || !parsedContent.isArray()) {
        qWarning() << "Failed to parse the returned translation JSON.";
        return result;
    }

    QJsonArray translationsArray = parsedContent.array();
    for (const QJsonValue &val : translationsArray) {
        if (!val.isObject())
            continue;
        QJsonObject obj = val.toObject();
        QString source = obj["source"].toString();
        QString translation = obj["translation"].toString();
        if (!source.isEmpty())
            result.insert(source, translation);
    }

    return result;
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

    // Define the command line options.
    QCommandLineOption tsFilePathOption(QStringList() << "t" << "ts_file_path",
                                        "Path to the TS file.",
                                        "ts_file_path");
    QCommandLineOption apiKeyPathOption(QStringList() << "k" << "api_key_path",
                                        "Path to the GPT API key file.",
                                        "api_key_path");
    QCommandLineOption apiCallSizeOption(QStringList() << "s" << "api_call_size",
                                         "Number of words per API call.",
                                         "api_call_size",
                                         "50"); // default is 50
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

    // Validate required options.
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
    QMap<QString, QMap<QString, QString>> translations = parseTsFile(tsFilePath);

    // Batch processing: accumulate untranslated source phrases based on total word count.
    QStringList batchPhrases;
    int currentWordCount = 0;

    // Iterate over each context and message.
    for (auto contextIt = translations.begin(); contextIt != translations.end(); ++contextIt) {
        QMap<QString, QString> &msgMap = contextIt.value();
        for (auto msgIt = msgMap.begin(); msgIt != msgMap.end(); ++msgIt) {
            if (!msgIt.key().isEmpty() && msgIt.value().isEmpty()) {
                int phraseWordCount = msgIt.key().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
                if ((currentWordCount + phraseWordCount) > apiCallSize && !batchPhrases.isEmpty()) {
                    QMap<QString, QString> batchResult = sendTranslationBatch(batchPhrases, apiKey, lang, langPostfix);
                    for (auto ctxIt = translations.begin(); ctxIt != translations.end(); ++ctxIt) {
                        QMap<QString, QString> &innerMap = ctxIt.value();
                        for (auto innerIt = innerMap.begin(); innerIt != innerMap.end(); ++innerIt) {
                            if (batchResult.contains(innerIt.key()))
                                innerIt.value() = batchResult.value(innerIt.key());
                        }
                    }
                    batchPhrases.clear();
                    currentWordCount = 0;
                }
                if (!batchPhrases.contains(msgIt.key())) {
                    batchPhrases.append(msgIt.key());
                    currentWordCount += phraseWordCount;
                }
            }
        }
    }
    if (!batchPhrases.isEmpty()) {
        QMap<QString, QString> batchResult = sendTranslationBatch(batchPhrases, apiKey, lang, langPostfix);
        for (auto ctxIt = translations.begin(); ctxIt != translations.end(); ++ctxIt) {
            QMap<QString, QString> &innerMap = ctxIt.value();
            for (auto innerIt = innerMap.begin(); innerIt != innerMap.end(); ++innerIt) {
                if (batchResult.contains(innerIt.key()))
                    innerIt.value() = batchResult.value(innerIt.key());
            }
        }
    }

    // Print the updated translations.
    for (auto contextIt = translations.begin(); contextIt != translations.end(); ++contextIt) {
        qDebug() << "Context:" << contextIt.key();
        const QMap<QString, QString> &msgMap = contextIt.value();
        for (auto msgIt = msgMap.begin(); msgIt != msgMap.end(); ++msgIt) {
            qDebug() << "   Source:" << msgIt.key() << "-> Translation:" << msgIt.value();
        }
    }

    return 0;
}
