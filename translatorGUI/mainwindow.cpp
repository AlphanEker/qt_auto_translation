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
}

MainWindow::~MainWindow()
{
    delete ui;
}

/// @brief Handles translation when the user clicks the "Translate" button in the GUI.
/// @details This function reads the current GUI values (language, CSV and TS checkboxes), updates the JSON
/// config file accordingly, and then spawns a new QProcess to run the CLI translation executable
/// (`qt_auto_translation.exe`) with the `-c` argument pointing to the config file.
/// It also connects to the process's stdout/stderr signals to log output to the application console,
/// and shows a completion dialog when the process finishes.
void MainWindow::handleTranslateButton() {
    qDebug() << "Starting handleTranslateButton";

    // Determine config file path relative to current working directory
    QString configPath = QDir::cleanPath(QDir::current().filePath("../../../auto_translator_cfg.json"));
    qDebug() << "Config path:" << configPath;

    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Error", "Could not open config file:\n" + configPath);
        qDebug() << "Failed to open config file";
        return;
    }
    configFile.close();

    // Read and update config JSON with UI values
    QJsonDocument doc;
    {
        QFile file(configPath);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, "Error", "Could not open config file to read JSON:\n" + configPath);
            qDebug() << "Failed to open config file to read JSON";
            return;
        }
        doc = QJsonDocument::fromJson(file.readAll());
        file.close();
    }
    if (!doc.isObject()) {
        QMessageBox::critical(this, "Error", "Invalid config JSON format.");
        qDebug() << "Invalid config JSON format";
        return;
    }

    QJsonObject config = doc.object();
    config["lang"] = ui->languageLineEdit->text();
    config["lang_postfix"] = ui->languagePostfixLineEdit->text();
    config["export_to_csv"] = ui->exportToCSVCheckBox->isChecked();
    config["import_from_csv"] = ui->importFromCSVCheckBox->isChecked();
    config["write_back_to_ts"] = ui->writeBackToTSCheckBox->isChecked();

    {
        QFile file(configPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::critical(this, "Error", "Could not write updated config file:\n" + configPath);
            qDebug() << "Failed to open config file for writing";
            return;
        }
        file.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
        file.close();
        qDebug() << "Config file updated";
    }

    // Prepare CLI path and arguments
    QString cliPath = QDir::cleanPath(QDir::current().filePath("../../../build/Desktop_Qt_6_8_2_MSVC2022_64bit-Debug/qt_auto_translation.exe"));
    qDebug() << "CLI path:" << cliPath;
    QStringList arguments;
    arguments << "-c" << configPath;

    // Create process on heap so it stays alive
    QProcess *process = new QProcess(this);

    // Connect signals for live output streaming
    connect(process, &QProcess::readyReadStandardOutput, this, [=]() {
        QByteArray out = process->readAllStandardOutput();
        qDebug() << "[Process stdout]" << out;
    });

    connect(process, &QProcess::readyReadStandardError, this, [=]() {
        QByteArray err = process->readAllStandardError();
        qDebug() << "[Process stderr]" << err;
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int exitCode, QProcess::ExitStatus exitStatus) {
                qDebug() << "Process finished with exit code" << exitCode << "and status" << exitStatus;
                QString msg = "Translation finished.\nExit code: " + QString::number(exitCode);
                QMessageBox::information(this, "Translation Done", msg);
                process->deleteLater();
            });

    qDebug() << "Starting process...";
    process->start(cliPath, arguments);

    if (!process->waitForStarted()) {
        QMessageBox::critical(this, "Error", "Failed to start translation executable:\n" + cliPath);
        qDebug() << "Failed to start translation executable";
        process->deleteLater();
        return;
    }
}

