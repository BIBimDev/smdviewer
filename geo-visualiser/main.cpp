#include "window.hpp"
#include "fixtures.hpp"
#include <QApplication>
#include <QMessageBox>
#include <iostream>
int main(int argc, char** argv) {
    QApplication application(argc,argv);
    try {
        inspector::Window window{std::filesystem::path(inspectorSampleDirectory),
                                 std::filesystem::path(inspectorExportDirectory)};
        window.show();
        return application.exec();
    } catch(const std::exception& e) {
        std::cerr << "GEO-Visualiser: " << e.what() << '\n';
        QMessageBox::critical(nullptr,"GEO-Visualiser",e.what());
        return 1;
    }
}
