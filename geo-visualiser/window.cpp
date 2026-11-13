#include "window.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <Standard_Failure.hxx>
#include <sstream>
#include <iomanip>

namespace inspector {
Window::Window(std::filesystem::path samples) : samples_(std::move(samples)) {
    setWindowTitle("GEO-Visualiser | Native OCCT geometry inspector");
    resize(1400,900);
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    auto* controls = new QHBoxLayout;
    layout->addLayout(controls);
    const auto combo = [&](const char* label, const char* objectName) {
        controls->addWidget(new QLabel(label));
        auto* result = new QComboBox;
        result->setObjectName(objectName);
        result->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        result->setMinimumContentsLength(12);
        controls->addWidget(result);
        return result;
    };
    specimen_ = combo("Specimen", "specimen");
    for(const auto& s : specimens) specimen_->addItem(s.label);
    representation_ = combo("Representation", "representation");
    for(auto r : {Representation::Faces,Representation::Solid,Representation::Candidate,Representation::Extensions,Representation::Reductions})
        representation_->addItem(name(r));
    representation_->setCurrentIndex(1);
    component_ = combo("Isolate", "component");
    pick_ = combo("Pick", "pick");
    for(auto k : {Kind::Vertex,Kind::Edge,Kind::Face,Kind::Body}) pick_->addItem(name(k));
    pick_->setCurrentIndex(2);
    auto* fit = new QPushButton("Fit");
    controls->addWidget(fit);
    status_ = new QLabel("Loading original fixture...");
    status_->setObjectName("status");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* split = new QSplitter;
    layout->addWidget(split,1);
    auto* sidebar = new QWidget;
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    tree_ = new QTreeWidget;
    tree_->setObjectName("entities");
    tree_->setHeaderLabels({"Entity / source identity", "User tag"});
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    sidebarLayout->addWidget(tree_,2);
    details_ = new QPlainTextEdit;
    details_->setObjectName("details");
    details_->setReadOnly(true);
    sidebarLayout->addWidget(details_,1);
    auto* tagLayout = new QHBoxLayout;
    tag_ = new QLineEdit;
    tag_->setObjectName("tag");
    tag_->setPlaceholderText("Session tag for selected entity");
    auto* saveTag = new QPushButton("Add / rename");
    saveTag->setObjectName("saveTag");
    auto* removeTag = new QPushButton("Remove");
    removeTag->setObjectName("removeTag");
    tagLayout->addWidget(tag_);
    tagLayout->addWidget(saveTag);
    tagLayout->addWidget(removeTag);
    sidebarLayout->addLayout(tagLayout);
    split->addWidget(sidebar);
    viewport_ = new Viewport;
    split->addWidget(viewport_);
    split->setSizes({440,960});
    layout->addWidget(new QLabel("Raw source-local mm | Left drag: orbit | Right/middle drag: pan | Wheel: zoom | Click: pick | Tags last for this run"));
    connect(specimen_,&QComboBox::currentIndexChanged,this,[&](int i) { load(i); });
    connect(representation_,&QComboBox::currentIndexChanged,this,[&] { representation(); });
    connect(component_,&QComboBox::currentIndexChanged,this,[&] { showComponents(); });
    connect(pick_,&QComboBox::currentIndexChanged,this,[&](int i) {
        select("",true);
        viewport_->setPickMode(static_cast<Kind>(i));
    });
    connect(fit,&QPushButton::clicked,this,[&] { viewport_->fit(); });
    connect(tree_,&QTreeWidget::currentItemChanged,this,[&](QTreeWidgetItem* item) {
        select(item ? item->data(0,Qt::UserRole).toString().toStdString() : "",false);
    });
    connect(saveTag,&QPushButton::clicked,this,[&] { tag(false); });
    connect(removeTag,&QPushButton::clicked,this,[&] { tag(true); });
    connect(tag_,&QLineEdit::returnPressed,this,[&] { tag(false); });
    viewport_->selectionChanged = [&](const std::string& id) { select(id,true); };
    // Defer clearing data until the viewport's current OCCT callback has returned.
    viewport_->failed = [&](const std::string& message) { QTimer::singleShot(0,this,[this,message] { fail(message); }); };
    QTimer::singleShot(0,this,[this] { load(0); });
}
Window::~Window() {
    // Release OCCT/GL while the parent's native window is still alive and
    // before the display-model members referenced by the viewport are destroyed.
    viewport_->selectionChanged = {};
    viewport_->failed = {};
    delete viewport_;
}
void Window::fail(const std::string& message) {
    viewport_->showDisplay(nullptr);
    viewport_->hide();
    document_.reset();
    display_.components.clear();
    items_.clear();
    { QSignalBlocker block(tree_); tree_->clear(); }
    selected_.clear();
    tag_->clear(); tag_->setEnabled(false);
    status_->setText(QString::fromStdString("Unavailable: " + message));
    details_->setPlainText("No validated results exposed. Check the fixture/dependency paths and restart after correcting the error.");
}
void Window::load(int index) {
    viewport_->showDisplay(nullptr);
    document_.reset();
    try {
        if(!cache_.at(index)) cache_[index] = loadDocument(index,samples_);
        document_ = cache_[index];
        representation();
    } catch(const std::exception& e) { fail(e.what()); }
}
void Window::representation() {
    if(!document_) return;
    viewport_->showDisplay(nullptr);
    try {
        auto prepared = prepareDisplay(*document_,static_cast<Representation>(representation_->currentIndex()));
        display_ = std::move(prepared);
        {
            QSignalBlocker block(component_);
            component_->clear(); component_->addItem("All");
            for(const auto& c : display_.components) component_->addItem(QString::fromStdString(c.label));
        }
        component_->setEnabled(!display_.components.empty());
        viewport_->show();
        showComponents();
    } catch(const Standard_Failure& e) { fail(e.what()); }
    catch(const std::exception& e) { fail(e.what()); }
}
void Window::showComponents() {
    if(!document_) return;
    selected_.clear();
    items_.clear();
    const int isolated = component_->currentIndex()-1;
    {
        QSignalBlocker block(tree_);
        tree_->clear();
        for(std::size_t i=0; i<display_.components.size(); ++i) {
            if(isolated>=0 && i!=static_cast<std::size_t>(isolated)) continue;
            const auto& c = display_.components[i];
            auto* root = new QTreeWidgetItem(tree_,{QString::fromStdString(c.label)});
            for(const auto& e : c.entities) {
                std::string label = e.id.substr(c.id.size()+1);
                if(e.sourceFace) label += " | source face " + std::to_string(*e.sourceFace);
                if(e.sourceVertex) label += " | source vertex " + std::to_string(*e.sourceVertex);
                if(e.sourceEndpoints) label += " | source " + std::to_string(e.sourceEndpoints->first) + "-" + std::to_string(e.sourceEndpoints->second);
                auto* item = new QTreeWidgetItem(root,{QString::fromStdString(label),QString::fromStdString(tags_.get(e.id))});
                item->setData(0,Qt::UserRole,QString::fromStdString(e.id));
                items_[e.id] = item;
            }
            root->setExpanded(true);
        }
        if(display_.components.empty()) new QTreeWidgetItem(tree_,{"Empty group — no components"});
        tree_->resizeColumnToContents(0);
    }
    viewport_->showDisplay(&display_,isolated);
    details(nullptr);
    std::ostringstream out;
    out << std::setprecision(17) << name(display_.representation) << " | " << display_.components.size() << " component(s)";
    if(display_.components.empty()) out << " — empty";
    if(display_.representation == Representation::Candidate) out << " | supporting source face " << document_->candidate.supportingFaceOrdinal << " (candidate-level evidence only)";
    const auto* parts = display_.representation == Representation::Extensions ? &document_->decomposition.extensions :
                        display_.representation == Representation::Reductions ? &document_->decomposition.reductions : nullptr;
    if(parts) for(std::size_t i=0;i<parts->size();++i) out << " | body " << i << ": " << (*parts)[i].volumeMm3 << " mm³";
    status_->setText(QString::fromStdString(out.str()));
}
const Entity* Window::entity(const std::string& id) const {
    for(const auto& c : display_.components) for(const auto& e : c.entities) if(e.id == id) return &e;
    return nullptr;
}
void Window::select(const std::string& id, bool fromViewport) {
    const auto* e = entity(id);
    if(!items_.contains(id)) e = nullptr;
    selected_ = e ? id : "";
    if(e && !fromViewport) {
        { QSignalBlocker block(pick_); pick_->setCurrentIndex(static_cast<int>(e->kind)); }
        viewport_->setPickMode(e->kind);
        if(!viewport_->select(id)) { selected_.clear(); e = nullptr; }
    } else if(!e) viewport_->select("");
    {
        QSignalBlocker block(tree_);
        tree_->setCurrentItem(e ? items_.at(id) : nullptr);
        if(e) tree_->scrollToItem(items_.at(id));
    }
    details(e);
}
void Window::details(const Entity* e) {
    tag_->setEnabled(e!=nullptr);
    tag_->setText(e ? QString::fromStdString(tags_.get(e->id)) : "");
    details_->setPlainText(e && document_ ? QString::fromStdString(describe(*document_,*e)) :
        "Select one entity in the viewport or tree.\nConverted faces are a face collection, not a solid.\nB is a derived candidate. A and R retain recorded Boolean ancestry.\nSource identity, viewer ID, derived lineage and user tags are separate.");
}
void Window::tag(bool remove) {
    if(selected_.empty()) return;
    tags_.set(selected_,remove ? "" : tag_->text().toStdString());
    items_.at(selected_)->setText(1,QString::fromStdString(tags_.get(selected_)));
    details(entity(selected_));
}
}
