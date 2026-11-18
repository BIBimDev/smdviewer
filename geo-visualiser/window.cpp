#include "window.hpp"
#include "appearance_settings.hpp"
#include "build_info.hpp"
#include <Standard_Version.hxx>
#include <QToolButton>
#include <QMenu>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QKeyEvent>
#include <QStatusBar>
#include <QTextEdit>
#include <QCheckBox>
#include <Standard_Failure.hxx>
#include <sstream>
#include <iomanip>

namespace inspector {
Window::Window(std::filesystem::path samples) : samples_(std::move(samples)) {
    preferences_.setFallbacksEnabled(false);
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
    auto* menuButton=new QToolButton;
    menuButton->setText("☰");menuButton->setAccessibleName("Menu");menuButton->setToolTip("Menu");
    menuButton->setObjectName("mainMenu");menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu=new QMenu(menuButton);menuButton->setMenu(menu);
    auto* settingsAction=menu->addAction("Settings…",this,[this] { settings(); });
    settingsAction->setObjectName("settingsAction");
    auto* helpAction=menu->addAction("Help",this,[this] { help(); });helpAction->setObjectName("helpAction");
    controls->addWidget(menuButton);
    auto* openButton = new QPushButton("Open SMD…");
    openButton->setObjectName("openSmd");
    controls->addWidget(openButton);
    filename_ = new QLabel;
    filename_->setObjectName("filename");
    filename_->setTextFormat(Qt::PlainText);
    controls->addWidget(filename_);
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
    auto* presentationControls = new QHBoxLayout;
    layout->addLayout(presentationControls);
    presentationControls->addWidget(new QLabel("Display style"));
    auto* style = new QComboBox;
    style->setObjectName("displayStyle");
    style->addItems({"Shaded","Transparent","Wireframe"});
    presentationControls->addWidget(style);
    auto* context = new QCheckBox("Show context");
    context->setObjectName("showContext");
    context->setChecked(true);
    presentationControls->addWidget(context);
    presentationControls->addStretch();
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
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_,&QTreeWidget::customContextMenuRequested,this,&Window::tagMenu);
    split->addWidget(sidebar);
    viewport_ = new Viewport;
    viewport_->setAppearance(loadAppearance(preferences_));
    connect(style,&QComboBox::currentIndexChanged,this,[this](int i) { viewport_->setDisplayStyle(static_cast<DisplayStyle>(i)); });
    connect(context,&QCheckBox::toggled,this,[this](bool show) { viewport_->setShowContext(show); });
    split->addWidget(viewport_);
    split->setSizes({440,960});
    connect(openButton,&QPushButton::clicked,this,[&] { open(); });
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
    projection_=new QLabel;
    projection_->setObjectName("projection");
    statusBar()->addPermanentWidget(projection_);
    viewport_->projectionChanged=[&] {
        projection_->setText(viewport_->perspective()?"P · Perspective":"O · Orthographic");
    };
    viewport_->projectionChanged();
    qApp->installEventFilter(this);
    viewport_->selectionChanged = [&](const std::string& id) { select(id,true); };
    // Defer clearing data until the viewport's current OCCT callback has returned.
    viewport_->failed = [&](const std::string& message) { QTimer::singleShot(0,this,[this,message] { fail(message); }); };
    QTimer::singleShot(0,this,[this] { load(samples_ / "plain_column.smd"); });
}
Window::~Window() {
    // Release OCCT/GL while the parent's native window is still alive and
    // before the display-model members referenced by the viewport are destroyed.
    qApp->removeEventFilter(this);
    viewport_->projectionChanged = {};
    viewport_->selectionChanged = {};
    viewport_->failed = {};
    delete viewport_;
}
bool Window::eventFilter(QObject* watched,QEvent* event) {
    if(event->type()!=QEvent::KeyPress) return QMainWindow::eventFilter(watched,event);
    const auto* key=static_cast<QKeyEvent*>(event);
    auto* widget=qobject_cast<QWidget*>(watched);
    if(!widget || widget->window()!=this || QApplication::activeModalWidget() ||
       key->modifiers()!=Qt::NoModifier)
        return QMainWindow::eventFilter(watched,event);
    // Keep text input and dialog shortcuts entirely within their owning widget.
    for(auto* start:{widget,QApplication::focusWidget()})
        for(auto* focus=start;focus;focus=focus->parentWidget())
            if(qobject_cast<QLineEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus) || qobject_cast<QTextEdit*>(focus))
                return QMainWindow::eventFilter(watched,event);
    int representation=-1;
    switch(key->key()) {
    case Qt::Key_S: representation=static_cast<int>(Representation::Solid);break;
    case Qt::Key_B: representation=static_cast<int>(Representation::Candidate);break;
    case Qt::Key_A: representation=static_cast<int>(Representation::Extensions);break;
    case Qt::Key_R: representation=static_cast<int>(Representation::Reductions);break;
    case Qt::Key_P: break;
    default: return QMainWindow::eventFilter(watched,event);
    }
    if(!key->isAutoRepeat()) {
        if(representation>=0) representation_->setCurrentIndex(representation);
        else viewport_->toggleProjection();
    }
    return true;
}
void Window::fail(const std::string& message) {
    viewport_->showDisplay(nullptr);
    viewport_->hide();
    document_.reset();
    display_.components.clear();
    display_.context.clear();
    items_.clear();
    { QSignalBlocker block(tree_); tree_->clear(); }
    selected_.clear();
    status_->setText(QString::fromStdString("Unavailable: " + message));
    details_->setPlainText("No validated results exposed. Check the fixture/dependency paths and restart after correcting the error.");
}
void Window::open() {
    // A fresh explicit directory on every invocation overrides Qt's last-used path.
    QFileDialog dialog(this, "Open SMD", QString::fromStdU16String(samples_.u16string()), "SMD files (*.smd)");
    dialog.setObjectName("openSmdDialog");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    if(dialog.exec() != QDialog::Accepted) return;
    const auto files = dialog.selectedFiles();
    if(!files.isEmpty()) load(std::filesystem::path(files.front().toStdU16String()));
}
bool Window::load(const std::filesystem::path& file) {
    std::shared_ptr<const Document> pending;
    Display prepared{Representation::Solid,{}, {}};
    try {
        pending = loadDocument(file); // Always reread and rerun the full pipeline.
        try { prepared = prepareDisplay(*pending,Representation::Solid); }
        catch(const Standard_Failure& e) { throw std::runtime_error(pathText(file) + ": prepareDisplay: " + e.what()); }
        catch(const std::exception& e) { throw std::runtime_error(pathText(file) + ": prepareDisplay: " + e.what()); }
    } catch(const std::exception& e) {
        // Keep every active document/view/editor field intact on load failure.
        if(document_) {
            QMessageBox warning(QMessageBox::Warning,"Open SMD failed",QString::fromUtf8(e.what()),QMessageBox::Ok,this);
            warning.setTextFormat(Qt::PlainText);
            warning.exec();
        } else fail(e.what());
        return false;
    }
    // Remove the old AIS objects and pick owners before releasing their mappings.
    viewport_->showDisplay(nullptr);
    document_ = std::move(pending);
    display_ = std::move(prepared);
    {
        QSignalBlocker representationBlock(representation_), componentBlock(component_), pickBlock(pick_);
        representation_->setCurrentIndex(static_cast<int>(Representation::Solid));
        component_->clear(); component_->addItem("All");
        for(const auto& c : display_.components) component_->addItem(QString::fromStdString(c.label));
        component_->setCurrentIndex(0);
        component_->setEnabled(!display_.components.empty());
        pick_->setCurrentIndex(static_cast<int>(Kind::Face));
    }
    filename_->setText(QString::fromStdU16String(file.filename().u16string()));
    filename_->setToolTip(QString::fromStdU16String(file.u16string()));
    viewport_->setPickMode(Kind::Face);
    viewport_->show();
    showComponents();
    viewport_->resetCamera();
    return true;
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
    // Representation/isolation changes preserve the comparison camera. Successful
    // Open explicitly resets/Fits; the Fit button frames only foreground copies.
    viewport_->showDisplay(&display_,isolated,false);
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
    details_->setPlainText(e && document_ ? QString::fromStdString(describe(*document_,*e)) :
        "Select one entity in the viewport or tree.\nConverted faces are a face collection, not a solid.\nB is a derived candidate. A and R retain recorded Boolean ancestry.\nSource identity, viewer ID, derived lineage and user tags are separate.");
}
void Window::tagMenu(const QPoint& point) {
    auto* row=tree_->itemAt(point);
    if(!row) return;
    const auto id=row->data(0,Qt::UserRole).toString().toStdString();
    if(!entity(id) || !items_.contains(id)) return;
    // Capture the invoked identity, never the previously selected row.
    const auto existing=QString::fromStdString(tags_.get(id));
    QMenu menu(this);menu.setObjectName("tagContextMenu");
    auto* edit=menu.addAction(existing.isEmpty()?"Add tag…":"Edit tag…");edit->setObjectName("editTagAction");
    QAction* remove=nullptr;
    if(!existing.isEmpty()) { remove=menu.addAction("Remove tag");remove->setObjectName("removeTagAction"); }
    const auto action=menu.exec(tree_->viewport()->mapToGlobal(point));
    if(!action) return;
    QString value;
    if(action==edit) {
        QInputDialog dialog(this);dialog.setObjectName("sessionTagDialog");
        dialog.setWindowTitle(existing.isEmpty()?"Add session tag":"Edit session tag");
        dialog.setLabelText("User tag (this session only; not SMD metadata)");
        dialog.setTextValue(existing);
        if(dialog.exec()!=QDialog::Accepted) return;
        value=dialog.textValue();
    } else if(action!=remove) return;
    tags_.set(id,value.toStdString());
    items_.at(id)->setText(1,QString::fromStdString(tags_.get(id)));
}
void Window::settings() {
    AppearanceDialog dialog(viewport_->appearance(),preferences_,[this](const Appearance& a) { viewport_->setAppearance(a); },this);
    dialog.exec();
}
void Window::help() {
    QDialog dialog(this);dialog.setObjectName("helpDialog");dialog.setWindowTitle("Help — GEO-Visualiser");dialog.resize(640,620);
    auto* layout=new QVBoxLayout(&dialog);
    auto* text=new QTextBrowser;text->setObjectName("helpText");
    text->setHtml(QString::fromUtf8(R"(<h2>Inspect geometry</h2>
<p><b>S</b>: validated solid; <b>B</b>: principal extrusion candidate;
<b>A</b>: extensions (S ∖ B); <b>R</b>: reductions (B ∖ S), showing missing material.
These keys match Representation. Converted faces remain a face collection.
B/A/R are derived interpretations, not original source metadata.</p>
<p><b>P</b> toggles Orthographic/Perspective; the status bar always shows the active projection.
Shortcuts are inactive while typing or using a dialog.</p>
<p><b>Orientation cube:</b> click a face centre for an axial view, an edge tile for an edge view,
or a corner tile for an isometric view. Drag with the left button on the cube to orbit.
Cube clicks preserve the projection and selected entity.</p>
<p><b>Navigate:</b> left drag orbits; right/middle drag pans; wheel zooms. Fit frames the active geometry.
Left click picks using the Vertex, Edge, Face or Body mode. The tree and viewport share selection.
Click background to clear selection. Isolate limits the active component; Show context adds passive outlines.</p>
<h3>Coordinates and scale</h3>
<p>Coordinates are <b>raw source-local millimetres</b>. No B-rep, element or project placement is applied.
In Perspective, the scale marked <b>“at target”</b> applies at the camera target plane;
objects at other depths have a different apparent scale.</p>
<h3>Session tags</h3>
<p>Right-click an entity row and choose Add tag, Edit tag or Remove tag. The action affects that row,
even if another row is selected. Grouping rows and blank space have no tag actions.
Cancel leaves the tag unchanged. Tags appear in the User tag column.</p>
<p>Tags belong to the canonical file path, exact file bytes and viewer entity identity.
They survive reopening that unchanged file in this window/run; other paths or changed bytes have separate tags.
Closing the session clears tags. A tag is not SMD metadata and never changes source evidence.</p>
<h3>Appearance</h3>
<p>Settings previews B/A/R sRGB colours and transparency in the current style. 0% is opaque;
72% is the factory transparency. Default values previews the factory settings. OK saves appearance locally;
Cancel, Escape or closing Settings restores its opening appearance. Tags and document state are not saved.</p>
<h3>About</h3><p>GEO-Visualiser — native SMD geometry inspector<br>Build: %1<br>OCCT: %2</p>)")
        .arg(QString::fromUtf8(inspectorBuild).toHtmlEscaped(),QString::fromLatin1(OCC_VERSION_COMPLETE)));
    layout->addWidget(text);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);dialog.exec();
}
}
