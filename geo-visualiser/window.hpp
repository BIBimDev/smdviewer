#pragma once
#include "viewport.hpp"
#include <QMainWindow>
#include <QTreeWidget>
#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QPlainTextEdit>

namespace inspector {
class Window : public QMainWindow {
public:
    Window(std::filesystem::path samples, std::filesystem::path exportDirectory);
    ~Window() override;
protected:
    bool eventFilter(QObject*, QEvent*) override;
private:
    void open();
    void exportGeometry();
    bool load(const std::filesystem::path& file);
    void representation();
    void showComponents();
    void select(const std::string& id, bool fromViewport);
    void details(const Entity*);
    void tagMenu(const QPoint&);
    void settings();
    void help();
    void setLoadStatus(const QString&);
    void fail(const std::string&);
    const Entity* entity(const std::string&) const;
    std::filesystem::path samples_, exportDirectory_;
    QAction* exportAction_;
    std::shared_ptr<const Document> document_;
    QString activePath_;
    Display display_{Representation::Solid,{}, {}};
    Tags tags_;
    std::string selected_;
    Viewport* viewport_;
    QComboBox *representation_, *component_, *pick_;
    QTreeWidget* tree_;
    QPlainTextEdit* details_;
    QSettings preferences_{QSettings::IniFormat,QSettings::UserScope,"BIBimDev","GEO-Visualiser"};
    QLabel* status_;
    QLabel* projection_;
    std::map<std::string,QTreeWidgetItem*> items_;
};
}
