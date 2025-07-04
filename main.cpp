#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

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
