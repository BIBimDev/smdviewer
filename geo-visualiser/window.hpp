#pragma once
#include "viewport.hpp"
#include <QMainWindow>
#include <QTreeWidget>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <array>

namespace inspector {
class Window : public QMainWindow {
public:
    explicit Window(std::filesystem::path samples);
    ~Window() override;
private:
    void load(int specimen);
    void representation();
    void showComponents();
    void select(const std::string& id, bool fromViewport);
    void details(const Entity*);
    void tag(bool remove);
    void fail(const std::string&);
    const Entity* entity(const std::string&) const;
    std::filesystem::path samples_;
    std::array<std::shared_ptr<const Document>, 2> cache_;
    std::shared_ptr<const Document> document_;
    Display display_{Representation::Solid,{}};
    Tags tags_;
    std::string selected_;
    Viewport* viewport_;
    QComboBox *specimen_, *representation_, *component_, *pick_;
    QTreeWidget* tree_;
    QPlainTextEdit* details_;
    QLineEdit* tag_;
    QLabel* status_;
    std::map<std::string,QTreeWidgetItem*> items_;
};
}
