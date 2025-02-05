#include <QFile>
#include <QXmlStreamReader>
#include <QMap>
#include <QString>
#include <QDebug>

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
            if (xml.name() == QLatin1String("context")) {  // Use QLatin1String here
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

int main()
{
    QString tsFilePath = "path/to/your/file.ts";
    QMap<QString, QMap<QString, QString>> translations = parseTsFile(tsFilePath);

    for (auto it = translations.begin(); it != translations.end(); ++it) {
        qDebug() << "Context:" << it.key();
        QMap<QString, QString> msgMap = it.value();
        for (auto msgIt = msgMap.begin(); msgIt != msgMap.end(); ++msgIt) {
            qDebug() << "   Source:" << msgIt.key() << "-> Translation:" << msgIt.value();
        }
    }

    return 0;
}
