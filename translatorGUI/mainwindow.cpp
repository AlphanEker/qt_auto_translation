#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QProcess>
#include <QMessageBox>
#include <QDir>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    connect(ui->translateButton, &QPushButton::clicked, this, &MainWindow::handleTranslateButton);
    connect(ui->clearButton, &QPushButton::clicked, this, &MainWindow::handleClearButton);
}

MainWindow::~MainWindow()
{
    delete ui;
}

/// @brief Loads the JSON configuration from a file.
/// @param configPath The path to the JSON config file.
/// @return A QJsonObject representing the loaded configuration.
/// Returns an empty object if the file cannot be opened or JSON is invalid.
static QJsonObject loadConfig(const QString &configPath) {
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open config file:" << configPath;
        return {};
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        qWarning() << "Invalid JSON in config file";
        return {};
    }

    return doc.object();
}

/// @brief Saves the given JSON configuration to a file.
/// @param configPath The path to the JSON config file.
/// @param config The QJsonObject to save.
/// @return True if saving succeeded; false otherwise.
static bool saveConfig(const QString &configPath, const QJsonObject &config) {
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open config file for writing:" << configPath;
        return false;
    }

    file.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

/// @brief Runs the translation CLI process with the specified config file.
/// @param cliPath The full path to the translation CLI executable.
/// @param configPath The full path to the JSON config file.
/// @param parent The QWidget parent for signal connections and message boxes.
static void runTranslationProcess(const QString &cliPath, const QString &configPath, QWidget *parent) {
    QProcess *process = new QProcess(parent);
    QStringList arguments = { "-c", configPath };

    QObject::connect(process, &QProcess::readyReadStandardOutput, parent, [=]() {
        QByteArray out = process->readAllStandardOutput();
        qDebug() << "[Process stdout]" << out;
    });

    QObject::connect(process, &QProcess::readyReadStandardError, parent, [=]() {
        QByteArray err = process->readAllStandardError();
        qDebug() << "[Process stderr]" << err;
    });

    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     parent,
                     [=](int exitCode, QProcess::ExitStatus exitStatus) {
                         qDebug() << "Process finished with exit code" << exitCode << "and status" << exitStatus;
                         QMessageBox::information(parent, "Translation Done",
                                                  "Translation finished.\nExit code: " + QString::number(exitCode));
                         process->deleteLater();
                     });

    process->start(cliPath, arguments);

    if (!process->waitForStarted()) {
        QMessageBox::critical(parent, "Error", "Failed to start translation executable:\n" + cliPath);
        qDebug() << "Failed to start translation executable";
        process->deleteLater();
    }
}


/// @brief Handles translation when the user clicks the "Translate" button in the GUI.
/// @details This function reads the current GUI values (language, CSV and TS checkboxes), updates the JSON
/// config file accordingly, and then spawns a new QProcess to run the CLI translation executable
/// (`qt_auto_translation.exe`) with the `-c` argument pointing to the config file.
/// It also connects to the process's stdout/stderr signals to log output to the application console,
/// and shows a completion dialog when the process finishes.
void MainWindow::handleTranslateButton() {
    QString configPath = QDir::cleanPath(QDir::current().filePath("../../../auto_translator_cfg.json"));
    QJsonObject config = loadConfig(configPath);
    if (config.isEmpty()) {
        QMessageBox::critical(this, "Error", "Failed to load config.");
        return;
    }

    QString langText = ui->languageLineEdit->text().trimmed();
    if (!langText.isEmpty()) {
        config["lang"] = langText;
    }

    QString langPostfixText = ui->languagePostfixLineEdit->text().trimmed();
    if (!langPostfixText.isEmpty()) {
        config["lang_postfix"] = langPostfixText;
    }

    config["export_to_csv"] = ui->exportToCSVCheckBox->isChecked();
    config["import_from_csv"] = ui->importFromCSVCheckBox->isChecked();
    config["write_back_to_ts"] = ui->writeBackToTSCheckBox->isChecked();
    config["clear_translation"] = false;

    if (!saveConfig(configPath, config)) {
        QMessageBox::critical(this, "Error", "Failed to save config.");
        return;
    }

    QString cliPath = QDir::cleanPath(QDir::current().filePath("../../../build/Desktop_Qt_6_8_2_MSVC2022_64bit-Debug/qt_auto_translation.exe"));
    runTranslationProcess(cliPath, configPath, this);
}


/// @brief Handles clearing the existing translation when the user clicks the "Clear" button
/// @details This function updates the clear_translation field in the JSON
/// config file, and then spawns a new QProcess to run the CLI translation executable
/// (`qt_auto_translation.exe`) with the `-c` argument pointing to the config file.
/// It later sets the clear_translation field in the config JSON file to false once again
/// after the clearing process finishes.
/// It also connects to the process's stdout/stderr signals to log output to the application console,
/// and shows a completion dialog when the process finishes.
void MainWindow::handleClearButton() {
    QString configPath = QDir::cleanPath(QDir::current().filePath("../../../auto_translator_cfg.json"));
    QJsonObject config = loadConfig(configPath);
    if (config.isEmpty()) {
        QMessageBox::critical(this, "Error", "Failed to load config.");
        return;
    }

    config["clear_translation"] = true;

    if (!saveConfig(configPath, config)) {
        QMessageBox::critical(this, "Error", "Failed to save config.");
        return;
    }

    QString cliPath = QDir::cleanPath(QDir::current().filePath("../../../build/Desktop_Qt_6_8_2_MSVC2022_64bit-Debug/qt_auto_translation.exe"));
    runTranslationProcess(cliPath, configPath, this);
}


