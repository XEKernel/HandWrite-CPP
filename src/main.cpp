#include <QApplication>
#include <QMessageBox>
#include "mainwindow.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("HandWrite");
    app.setApplicationVersion("2.4.0");
    app.setOrganizationName("HandWrite");
    
    try {
        HandWrite::MainWindow window;
        window.setWindowTitle("HandWrite Generator");
        window.show();
        return app.exec();
    } catch (const std::exception& e) {
        QMessageBox::critical(nullptr, "Error", QString("Exception: %1").arg(e.what()));
        return 1;
    } catch (...) {
        QMessageBox::critical(nullptr, "Error", "Unknown exception");
        return 1;
    }
}
