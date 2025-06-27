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

/// @brief Represents a source code location.
/// @details This structure stores the filename and line number where a particular event occurs.
struct Location {
    QString filename; ///< The name of the file where the location is referenced.
    int line; ///< The line number within the file.
};

/// @brief Holds information about a translation message.
/// @details This structure contains details about a message, including its source text,
/// translation, type, and multiple locations where it appears.
struct MessageInfo {
    QList<Location> locations; ///< List of locations where the message is found.
    QString source; ///< The original text of the message.
    QString translation; ///< The translated text.
    QString translationType; ///< The type of translation, e.g., "unfinished".
};

/// @brief Holds configuration settings for the translation process.
/// @details This structure stores file paths, API settings, and language options.
struct Config {
    QString tsFilePath;    ///< Path to the TS (translation source) file.
    QString apiKeyPath;    ///< Path to the API key file.
    int apiCallSize;       ///< Number of phrases per API call batch.
    QString lang;          ///< Target language for translation.
    QString langPostfix;   ///< Additional language specification (e.g., TR_tr, RU_ru).
    QString csvToImport;   ///< If the importFromCSV option is true this file will be imported and written into ts file.
    QString csvToExport;   ///< If the exportToCSV option is true this file will be written with the original source and translations.
    bool importFromCSV;    ///< If true the program will read from csv and write into ts. Won't make GPT calls.
    bool exportToCSV;      ///< If true translations will write into csv file.
    bool writeBackToTs;    ///< If true the TS file will be overwritten and the translations will be put into place.
};

/// @brief Parses a TS (Translation Source) file and extracts message information.
/// @details This function reads an XML-based TS file and maps context names to lists of messages.
/// Each message includes source text, translation, translation type, and location data.
///
/// @param filePath The path to the TS file to be parsed.
/// @return A QMap where keys are context names and values are lists of MessageInfo structures.
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

/// @brief Writes updated translations to a TS (Translation Source) file.
/// @details This function takes a mapping of context names to message lists and writes them
/// into an XML-based TS file. It preserves structure, including context names, message sources,
/// translations, and locations.
///
/// @param filePath The path to the TS file to be written.
/// @param translations A QMap where keys are context names and values are lists of MessageInfo structures.
/// @return True if the file was successfully written, false otherwise.
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

/// @brief Reads an API key from a specified file.
/// @details This function opens a file containing the API key as plain text, reads its contents,
/// and trims any extraneous whitespace or UTF-8 BOM if present.
///
/// @param apiKeyPath The path to the file containing the API key.
/// @return The API key as a QString, or an empty string if the file could not be read.
bool exportToCsv(const QString &csvFilePath, const QMap<QString, QList<MessageInfo>> &translations)
{
    QFile file(csvFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot open CSV file for writing:" << csvFilePath;
        return false;
    }
    QTextStream out(&file);

    // Required for Excel UTF-8 detection
    file.write("\xEF\xBB\xBF");  //OA

    // Write header (you can adjust the header order if desired).
    out << "name,filename,line,source,translation\n";

    // Helper lambda to escape a CSV field.
    auto escapeCsvField = [](const QString &field) -> QString {
        QString result = field;
        if (result.contains(QLatin1Char(',')) ||
            result.contains(QLatin1Char('"')) ||
            result.contains('\n'))
        {
            result.replace("\"", "\"\"");
            result = "\"" + result + "\"";
        }
        return result;
    };

    // Iterate over each context and its messages.
    for (auto contextIt = translations.constBegin(); contextIt != translations.constEnd(); ++contextIt) {
        const QString &contextName = contextIt.key(); //OA //string for the page name
        const QList<MessageInfo> &messages = contextIt.value();
        for (const MessageInfo &msg : messages) {

            for (const Location &loc : msg.locations) {
                out << escapeCsvField(contextName) << ","
                    << escapeCsvField(loc.filename) << ","
                    << loc.line << ","
                    << escapeCsvField(msg.source) << ","
                    << escapeCsvField(msg.translation) << "\n";
            }
        }
    }
    file.close();
    return true;
}

/// @brief Parses a single CSV line into a list of fields.
/// @details This function supports quoted fields, handling escaped quotes within them.
/// Fields are separated by commas.
///
/// @param line A single CSV line as a QString.
/// @return A QStringList containing the parsed fields.
QStringList parseCsvLine(const QString &line)
{
    QStringList result;
    QString field;
    bool inQuotes = false;
    for (int i = 0; i < line.length(); ++i) {
        QChar c = line.at(i);
        if (inQuotes) {
            if (c == '"') {
                // Look ahead for an escaped quote.
                if (i + 1 < line.length() && line.at(i + 1) == '"') {
                    field.append('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field.append(c);
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                result.append(field);
                field.clear();
            } else {
                field.append(c);
            }
        }
    }
    result.append(field);
    return result;
}

/// @brief Imports translations from a CSV file and updates the translation map.
/// @details This function reads a CSV file containing translation data and updates
/// the provided QMap of translations. The CSV format is expected to have at least
/// four columns: source text, translation, translation type, and locations.
/// Locations are expected in the format "filename:line", separated by semicolons.
///
/// @param csvFilePath The path to the CSV file to import.
/// @param translations A reference to a QMap where keys are context names and values
///                     are lists of MessageInfo structures. The function updates
///                     the translation fields of the messages.
/// @return True if the CSV file was successfully read and processed, false otherwise.
bool importFromCsv(const QString &csvFilePath, QMap<QString, QList<MessageInfo>> &translations)
{
    QFile file(csvFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open CSV file for reading:" << csvFilePath;
        return false;
    }
    QTextStream in(&file);
    // Read header line and ignore.
    if (!in.atEnd())
        in.readLine();

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty())
            continue;

        QStringList fields = parseCsvLine(line);
        if (fields.size() < 5) {
            qWarning() << "Invalid CSV line (not enough fields):" << line;
            continue;
        }

        QString name = fields[0].trimmed();
        QString filename = fields[1].trimmed();
        QString lineStr = fields[2].trimmed();
        QString source = fields[3].trimmed();
        QString translation = fields[4].trimmed();

        // If there's no translation provided, skip this row.
        if (translation.isEmpty())
            continue;

/*        // Parse the locations (expecting format "filename:line" separated by semicolons).
        QList<Location> newLocations;
        QStringList locList = line.split(";", Qt::SkipEmptyParts);
        for (QString locStr : locList) {
            locStr = locStr.trimmed();
            int colonIndex = locStr.lastIndexOf(':');
            if (colonIndex != -1) {
                QString filename = locStr.left(colonIndex).trimmed();
                bool ok = false;
                int lineNumber = locStr.mid(colonIndex + 1).trimmed().toInt(&ok);
                if (ok) {
                    newLocations.append({filename, lineNumber});
                }
            }
        }
*/

        bool ok = false;
        int lineNumber = lineStr.toInt(&ok);
        if(!ok) {
            qWarning() << "Invalid line number:" << lineStr << "in CSV line:" << line;
            continue;
        }

        Location newLocation{filename, lineNumber};
        QList<Location> newLocations;
        newLocations.append(newLocation);


/*        // For each matching message (by source text), update translation details.
        for (auto contextIt = translations.begin(); contextIt != translations.end(); ++contextIt) {
            QList<MessageInfo> &messages = contextIt.value();
            for (MessageInfo &msg : messages) {
                if (msg.source == source) {
                    msg.translation = translation;
                    msg.translationType = translationType;
                    // Optionally update locations if desired.
                    msg.locations = newLocations;
                }
            }
        }
*/
        // Find the context by name (contextName) //OA
        if (!translations.contains(name)) {
            qWarning() << "Context not found:" << name;
            continue;
        }

        QList<MessageInfo> &messages = translations[name];

        // Find the message by source text inside this context
        bool found = false;
        for (MessageInfo &msg : messages) {
            if (msg.source == source) {
                msg.translation = translation;
                found = true;
                break;
            }
        }

        if (!found) {
            qWarning() << "Message with source not found in context" << name << ":" << source;
            // Optionally add a new message here if needed
        }

    }
    file.close();
    return true;
}


/// @brief Reads an API key from a specified file.
/// @details This function opens a file containing the API key as plain text, reads its contents,
/// and trims any extraneous whitespace or UTF-8 BOM if present.
///
/// @param apiKeyPath The path to the file containing the API key.
/// @return The API key as a QString, or an empty string if the file could not be read.
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

/// @brief Sends a batch of phrases to the GPT API for translation.
/// @details This function constructs a request to the GPT API, formatting the phrases as a prompt.
/// The API response is expected to be a JSON array of objects with source and translated text.
///
/// @param phrases A list of phrases to be translated.
/// @param apiKey The API key used for authentication.
/// @param lang The target language for translation.
/// @param langPostfix (EN_en, TR_tr ...)
/// @return The raw response data from the API as a QByteArray, or an empty QByteArray if an error occurs.
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

/// @brief Processes an API response and updates the translation map.
/// @details This function parses the JSON response from the GPT API, extracts translations,
/// and updates the corresponding messages in the provided translation map.
///
/// @param responseData The raw API response data as a QByteArray.
/// @param translations A reference to a QMap where keys are context names and values are lists of MessageInfo.
///                     The function updates the translation fields of the messages.
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

/// @brief Loads configuration settings from a JSON file.
/// @details This function reads a JSON configuration file, parses its content,
/// and populates a Config structure with the extracted values.
/// Exits the program if the configuration file is missing or invalid.
///
/// @param configPath The path to the configuration file.
/// @return A Config structure populated with the loaded settings.
Config loadConfig(const QString &configPath) {

    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "Config dosyası açılamadı!";
        exit(1);
    }

    QByteArray jsonData = configFile.readAll();
    configFile.close();

    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData);
    if (!jsonDoc.isObject()) {
        qCritical() << "Config dosyası geçersiz!";
        exit(1);
    }

    QJsonObject jsonObj = jsonDoc.object();
    Config config;
    config.tsFilePath    = jsonObj["ts_file_path"].toString();
    config.apiKeyPath    = jsonObj["api_key_path"].toString();
    config.apiCallSize   = jsonObj["api_call_size"].toInt(50);
    config.lang          = jsonObj["lang"].toString();
    config.langPostfix   = jsonObj["lang_postfix"].toString();
    config.csvToExport   = jsonObj["csv_to_export"].toString();
    config.csvToImport   = jsonObj["csv_to_import"].toString();
    config.exportToCSV   = jsonObj["export_to_csv"].toBool();
    config.importFromCSV = jsonObj["import_from_csv"].toBool();
    config.writeBackToTs = jsonObj["write_back_to_ts"].toBool();

    return config;
}
//---------------------------------------------------------------------
// Main function
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("Qt GPT Translator");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Translate TS files using a GPT API");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configPathOption(QStringList() << "c" << "config_path",
                                        "Path to the config JSON file.",
                                        "config_path",
                                        "config.json");

    parser.addOption(configPathOption);
    parser.process(app);

    QString configPath = parser.value(configPathOption);
    Config config = loadConfig(configPath);

    qDebug() << "TS File Path:" << config.tsFilePath;
    qDebug() << "API Key Path:" << config.apiKeyPath;
    qDebug() << "API Call Size:" << config.apiCallSize;
    qDebug() << "Language:" << config.lang;
    qDebug() << "Language Postfix:" << config.langPostfix;

    // Read the API key.
    QString apiKey = readApiKeyFromFile(config.apiKeyPath);
    if (apiKey.isEmpty()) {
        qCritical() << "API key is empty or could not be read.";
        return 1;
    }

    // Parse the TS file.
    QMap<QString, QList<MessageInfo>> translations = parseTsFile(config.tsFilePath);

    if(config.importFromCSV){
        importFromCsv(config.csvToImport,translations);
    }
    else{
        // Batch processing: accumulate untranslated source phrases.
        QStringList batchPhrases;

        // Iterate over each context and its messages.
        for (auto contextIt = translations.begin(); contextIt != translations.end(); ++contextIt) {
            QList<MessageInfo> &messages = contextIt.value();
            for (MessageInfo &msg : messages) {
                if (!msg.source.isEmpty() && msg.translation.isEmpty()) {
                    batchPhrases.append(msg.source);
                    if (batchPhrases.size() >= config.apiCallSize) {
                        QByteArray responseData = sendTranslationBatch(batchPhrases, apiKey, config.lang, config.langPostfix);
                        processResponse(responseData, translations);
                        batchPhrases.clear();
                    }
                }
            }
        }

        // Process any remaining phrases.
        if (!batchPhrases.isEmpty()) {
            QByteArray responseData = sendTranslationBatch(batchPhrases, apiKey, config.lang, config.langPostfix);
            processResponse(responseData, translations);
            batchPhrases.clear();
        }
    }

    // Write the updated translations back to the TS file.
    if (config.writeBackToTs && !writeTsFile(config.tsFilePath, translations)) {
        qCritical() << "Failed to write back to TS file.";
        return 1;
    }

    // Export to csv to CSV if wanted
    if(config.exportToCSV){
        exportToCsv(config.csvToExport,translations);
    }

    return 0;
}
