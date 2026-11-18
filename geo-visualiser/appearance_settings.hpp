#pragma once
#include "appearance.hpp"
#include <QDialog>
#include <QSettings>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <array>
#include <functional>

namespace inspector {
Appearance loadAppearance(QSettings&);
bool saveAppearance(QSettings&, const Appearance&);
// A dialog transaction owns the opening snapshot; rejection never reads rounded UI values.
class AppearanceDialog : public QDialog {
public:
    AppearanceDialog(const Appearance&, QSettings&, std::function<void(const Appearance&)>, QWidget* parent=nullptr);
    void accept() override;
    void reject() override;
private:
    void refresh();
    void swatch(int);
    Appearance original_, preview_;
    QSettings& settings_;
    std::function<void(const Appearance&)> apply_;
    std::array<std::array<QSpinBox*,3>,3> channels_{};
    std::array<QLabel*,3> swatches_{};
    QSlider* transparency_;
    QLabel* percent_;
};
}
