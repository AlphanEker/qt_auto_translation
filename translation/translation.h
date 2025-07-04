 #ifndef TRANSLATION_H
#define TRANSLATION_H

#include <QString>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QByteArray>



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
    bool clearTranslation; ///< If true all the existing translation will be removed
};


QMap<QString, QList<MessageInfo>> parseTsFile(const QString &filePath);
bool writeTsFile(const QString &filePath, const QMap<QString, QList<MessageInfo>> &translations, const QString &languageCode); //OA added the language code argument
bool exportToCsv(const QString &csvFilePath, const QMap<QString, QList<MessageInfo>> &translations);
QStringList parseCsvLine(const QString &line);
bool importFromCsv(const QString &csvFilePath, QMap<QString, QList<MessageInfo>> &translations);
QString readApiKeyFromFile(const QString &apiKeyPath);
QByteArray sendTranslationBatch(const QStringList &phrases, const QString &apiKey, const QString &lang, const QString &langPostfix);
void processResponse(const QByteArray &responseData, QMap<QString, QList<MessageInfo>> &translations);
bool clearTranslation(const QString &filePath, const QString &csvFilePath, const QString &languageCode);
Config loadConfig(const QString &configPath);

#endif // TRANSLATION.H
