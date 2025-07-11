#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QFile>
#include <QFileInfo>
#include <QDir>

#include "translation.h"

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

    QString baseDir = QFileInfo(config.templateTsFile).absolutePath();
    QString tsFileName = QString("language_%1.ts").arg(config.langPostfix);
    QString tsFilePath = QDir(baseDir).filePath(tsFileName);


    // If TS file doesn't exist, create it from the template
    if (!QFile::exists(tsFilePath)) {
        qDebug() << "TS file does not exist. Creating from template...";
        if (!createTsFileFromTemplate(config.templateTsFile, tsFilePath, config.langPostfix)) {
            qCritical() << "Failed to create TS file from template.";
            return 1;
        }
    }

    // Now update the config to use this generated or existing file
    config.tsFilePath = tsFilePath;

    qDebug() << "TS File Path:" << config.tsFilePath;
    qDebug() << "API Key Path:" << config.apiKeyPath;
    qDebug() << "API Call Size:" << config.apiCallSize;
    qDebug() << "Language:" << config.lang;
    qDebug() << "Language Postfix:" << config.langPostfix;
    qDebug() << "Clear Translation:" << config.clearTranslation;

    // Parse the TS file.
    QMap<QString, QList<MessageInfo>> translations = parseTsFile(config.tsFilePath);

    // If clearTranslation is called, perform it and exit
    if (config.clearTranslation) {
        qDebug() << "Clearing translations as requested...";
        bool success = clearTranslation(config.tsFilePath, config.csvToExport, config.langPostfix);
        if (!success) {
            qCritical() << "Failed to clear translations.";
            return 1;
        }
        qDebug() << "Translations cleared successfully.";
        return 0;
    }


    // Read the API key.
    QString apiKey = readApiKeyFromFile(config.apiKeyPath);
    if (apiKey.isEmpty()) {
        qCritical() << "API key is empty or could not be read.";
        return 1;
    }

    if(config.importFromCSV){
        importFromCsv(config.csvToImport,translations);
    }
    else{
        for (auto contextIt = translations.begin(); contextIt != translations.end(); ++contextIt) {
            QList<MessageInfo> &messages = contextIt.value();
            const QString &contextName = contextIt.key();
            QStringList batchPhrases;

            for (MessageInfo &msg : messages) {
                if (!msg.source.isEmpty() && msg.translation.isEmpty()) {
                    batchPhrases.append(msg.source);
                    if (batchPhrases.size() >= config.apiCallSize) {
                        QByteArray responseData = sendTranslationBatch(batchPhrases, apiKey, config.lang, config.langPostfix, contextName);
                        processResponse(responseData, messages); // Only update this context
                        batchPhrases.clear();
                    }
                }
            }

            // ✅ Moved here: make sure remaining phrases in *this* context get processed
            if (!batchPhrases.isEmpty()) {
                QByteArray responseData = sendTranslationBatch(batchPhrases, apiKey, config.lang, config.langPostfix, contextName);
                processResponse(responseData, messages);  // Update just this context
                batchPhrases.clear();
            }
        }
    }

    // Write the updated translations back to the TS file.
    if (config.writeBackToTs && !writeTsFile(config.tsFilePath, translations, config.langPostfix)) {
        qCritical() << "Failed to write back to TS file.";
        return 1;
    }

    // Export to csv to CSV if wanted
    if(config.exportToCSV){
        exportToCsv(config.csvToExport,translations);
    }

    return 0;
}
