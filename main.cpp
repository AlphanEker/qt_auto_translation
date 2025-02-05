#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDebug>
#include <QFile>
#include <QXmlStreamReader>
#include <QMap>
#include <QString>

// Function to parse the TS file as before.
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
        parser.showHelp(1); // Exits the application.
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

    // parse the TS file.
    QMap<QString, QMap<QString, QString>> translations = parseTsFile(tsFilePath);
    for (auto contextIt = translations.begin(); contextIt != translations.end(); ++contextIt) {
        qDebug() << "Context:" << contextIt.key();
        const QMap<QString, QString> &messages = contextIt.value();
        for (auto msgIt = messages.begin(); msgIt != messages.end(); ++msgIt) {
            qDebug() << "   Source:" << msgIt.key() << "-> Translation:" << msgIt.value();
        }
    }

    return 0;
}
