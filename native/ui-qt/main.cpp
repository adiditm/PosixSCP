//---------------------------------------------------------------------------
// WinSCP native port — Qt 6 GUI, faithful to WinSCP's Commander interface.
//
// Skeleton mirrors WinSCP: an iconic Login dialog (sites tree + Session form) on startup, then
// the dual-pane Commander (local | remote) with per-panel address bars, the Name/Size/Changed/
// Rights/Owner columns, and the classic bottom function-key bar (F5 Copy / F6 Move / F7 Create
// Directory / F8 Delete / ...). Both panels talk to the ported engine through enginebridge.
//---------------------------------------------------------------------------
#include <cstdlib>
#include <cstdio>
#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QClipboard>
#include <QTreeView>
#include <QTabBar>
#include <QSplitter>
#include <QRegularExpression>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QComboBox>
#include <QTabWidget>
#include <QString>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTextCursor>
#include <QCloseEvent>
#include <QDockWidget>
#include <QCheckBox>
#include <QRadioButton>
#include <QMenu>
#include <QTimer>
#include <QSettings>
#include "updatecheck.h"
#include <QProgressDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QHash>
#include <QIcon>
#include <QFont>
#include <QStyle>
#include <algorithm>
#include <map>
#include <QEventLoop>
#include <functional>
#include <thread>
#include <atomic>

#include "enginebridge.h"
#include "uicommon.h"
#include "logindialog.h"
#include "transferdialog.h"
#include "finddialog.h"
#include "diffview.h"
#include "panelcompare.h"
#include "operationdialog.h"

#ifndef POSIXSCP_VERSION
#define POSIXSCP_VERSION "1.0.0"
#endif
#ifndef FREESCP_VERSION
#define FREESCP_VERSION POSIXSCP_VERSION
#endif

static bool gShowHidden = true;        // WinSCP "Show hidden files" toggle (dotfiles)
static bool gConfirmDelete = true;     // confirm before deleting
static bool gConfirmOverwrite = true;  // confirm before overwriting (stored; enforcement TBD)
static int  gParallelMax = 2;          // max concurrent connections for queue parallelism (1-4)
static bool gAltColors = true;         // alternating row colors in panels
static bool gCompareByTime = true;     // Compare Directories criteria (WinSCP defaults: time on, size off)
static bool gCompareBySize = false;
static bool gUseExternalEditor = false; // Editors: false = built-in editor, true = external editor
static QString gExternalEditorPath = ""; // Path or command for external editor (e.g. Visual Studio Code)
static bool gDoubleClickToEdit = true;  // Double-clicking a remote file opens it for editing
static std::atomic<bool> gTransferRunning{false};  // a background transfer batch is in flight

//===========================================================================
// Properties dialog — WinSCP-style permissions grid (Owner/Group/Others x R/W/X) with a synced
// octal field. Returns the chosen octal string, or empty if cancelled.
//===========================================================================
static QString showPropertiesDialog(QWidget * parent, const QString & name, const QString & curOctal,
                                    const QString & info, bool isDir, bool * recursiveOut)
{
  QDialog dlg(parent);
  dlg.setWindowTitle(name + " — Properties");

  auto * outer = new QVBoxLayout(&dlg);
  if (!info.isEmpty()) { auto * il = new QLabel(info); il->setTextInteractionFlags(Qt::TextSelectableByMouse); outer->addWidget(il); }
  auto * grp = new QGroupBox("Permissions");
  auto * g = new QGridLayout(grp);
  g->addWidget(new QLabel("R"), 0, 1, Qt::AlignHCenter);
  g->addWidget(new QLabel("W"), 0, 2, Qt::AlignHCenter);
  g->addWidget(new QLabel("X"), 0, 3, Qt::AlignHCenter);
  const char * rowNames[3] = { "Owner", "Group", "Others" };
  QCheckBox * cb[9];
  for (int row = 0; row < 3; ++row)
  {
    g->addWidget(new QLabel(rowNames[row]), row + 1, 0);
    for (int col = 0; col < 3; ++col)
    {
      cb[row * 3 + col] = new QCheckBox;
      g->addWidget(cb[row * 3 + col], row + 1, col + 1, Qt::AlignHCenter);
    }
  }
  outer->addWidget(grp);

  auto * octRow = new QHBoxLayout;
  octRow->addWidget(new QLabel("Octal:"));
  auto * octEdit = new QLineEdit;
  octRow->addWidget(octEdit);
  outer->addLayout(octRow);

  // seed from curOctal (last 3 digits)
  int initial = curOctal.right(3).toInt(nullptr, 8);
  for (int i = 0; i < 9; ++i) cb[i]->setChecked((initial >> (8 - i)) & 1);

  bool guard = false;
  auto syncOctalFromBoxes = [&] {
    if (guard) return; guard = true;
    int v = 0; for (int i = 0; i < 9; ++i) if (cb[i]->isChecked()) v |= (1 << (8 - i));
    octEdit->setText(QString("%1").arg(v, 3, 8, QChar('0')));
    guard = false;
  };
  auto syncBoxesFromOctal = [&] {
    if (guard) return; guard = true;
    int v = octEdit->text().toInt(nullptr, 8);
    for (int i = 0; i < 9; ++i) cb[i]->setChecked((v >> (8 - i)) & 1);
    guard = false;
  };
  for (int i = 0; i < 9; ++i) QObject::connect(cb[i], &QCheckBox::toggled, syncOctalFromBoxes);
  QObject::connect(octEdit, &QLineEdit::textEdited, syncBoxesFromOctal);
  syncOctalFromBoxes();

  auto * cbRec = new QCheckBox("Set recursively (apply to directory contents)");
  cbRec->setVisible(isDir);
  outer->addWidget(cbRec);

  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  outer->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) return QString();
  if (recursiveOut) *recursiveOut = cbRec->isChecked();
  return octEdit->text();
}

//===========================================================================
// File panel — a WinSCP-style pane: address bar + file list (Name/Size/Changed/Rights/Owner)
// + a per-panel status line. Local or remote, both via enginebridge.
//===========================================================================
class FilePanel : public QWidget
{
public:
  std::function<void()> onActivated;
  std::function<void()> onStatusChanged;
  std::function<void()> onHome;
  std::function<void()> onFileOpen;   // file (not dir) activated
  std::function<void(const QString &)> onEnterDir;  // user entered a subdir (for sync browsing)
  std::function<void()> onLeaveDir;                 // user went up (for sync browsing)
  std::function<void()> onSwitchPanel;              // Tab pressed -> focus the other panel
  std::function<void(FilePanel *, const QStringList &)> onDropFiles;  // files dropped from another panel
  std::function<void(FilePanel *, const QStringList &)> onDropExternal; // local files dropped from Finder
  std::function<void(const QString &)> onListError;   // a remote directory could not be opened/read
  std::function<void()> onUpload;
  std::function<void()> onDownload;
  std::function<void()> onMove;
  std::function<void()> onEdit;
  std::function<void()> onInternalEdit;
  std::function<void()> onDelete;
  std::function<void()> onProperties;
  std::function<void()> onCalculateDirSizes;
  std::function<void()> onNewFile;
  std::function<void()> onNewDir;
  std::function<void()> onSelectMask;
  std::function<void()> onUnselectMask;
  std::function<void(const QString &)> onApplyFilter;
  std::function<void(const QString &)> onAddBookmark;
  static FilePanel * s_dragSource;                  // panel a drag originated from

  // Mirror operations (used by synchronized browsing; navigate directly, no callbacks -> no loop).
  void enterSubdir(const QString & name)
  {
    for (const auto & e : FEntries)
      if (e.isDir && !e.isParent && u8(e.name) == name) { navigate(u8(engine::joinPath(s8(FPath), e.name))); return; }
  }
  void upOne() { navigate(u8(engine::parentDir(s8(FPath)))); }

  //--- directory tree (lazy, click-to-navigate) ---
  void setTreeVisible(bool v) { FTree->setVisible(v); if (v && FTreeModel->rowCount() == 0) treeReload(); }
  bool treeVisible() const { return FTree->isVisible(); }
  void treeReload() {
    FTreePopulating = true;
    FTreeModel->clear();
    auto * root = makeTreeNode("/", "/");
    FTreeModel->appendRow(root);
    populateTreeNode(root);
    FTree->expand(root->index());
    FTreePopulating = false;
  }
  QStandardItem * makeTreeNode(const QString & label, const QString & path) {
    auto * it = new QStandardItem(qApp->style()->standardIcon(QStyle::SP_DirIcon), label);
    it->setData(path, Qt::UserRole + 1);     // full path
    it->setData(false, Qt::UserRole + 2);    // populated?
    return it;
  }
  void populateTreeNode(QStandardItem * node) {
    if (node->data(Qt::UserRole + 2).toBool()) return;
    node->setData(true, Qt::UserRole + 2);
    node->removeRows(0, node->rowCount());   // drop the placeholder
    QString path = node->data(Qt::UserRole + 1).toString();
    for (const QString & name : subdirsOf(path)) {
      auto * child = makeTreeNode(name, u8(engine::joinPath(s8(path), s8(name))));
      child->appendRow(new QStandardItem());  // placeholder -> shows the expand arrow
      node->appendRow(child);
    }
  }
  void treeExpand(const QModelIndex & ix) { if (auto * n = FTreeModel->itemFromIndex(ix)) populateTreeNode(n); }
  QStringList subdirsOf(const QString & path) {
    QStringList dirs;
    if (FRemote && gTransferRunning.load()) return dirs;   // don't disturb the session mid-transfer
    auto entries = FRemote ? engine::listRemoteDir(s8(path)) : engine::listLocalDir(s8(path));
    for (const auto & e : entries) if (e.isDir && !e.isParent) dirs << u8(e.name);
    if (FRemote && path != FPath) engine::listRemoteDir(s8(FPath));  // restore session CWD + the panel's listing
    return dirs;
  }

  explicit FilePanel(QWidget * parent = nullptr) : QWidget(parent)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Title bar (host/protocol or "Local" - hidden in Commander mode to match WinSCP).
    FHeader = new QLabel("Local");
    FHeader->setStyleSheet("padding:4px 6px; font-weight:bold; background:palette(midlight);");
    FHeader->hide();

    // 1. Navigation Toolbar (Drive/Path combo, Bookmarks, Filter, Nav buttons, Path line)
    auto * navBar = new QWidget;
    navBar->setStyleSheet(
      "QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 1px 2px; margin: 0px; }"
      "QToolButton:hover { border: 1px solid #b8cfe5; background: #eef5fb; }"
      "QToolButton:pressed, QToolButton:checked { border: 1px solid #7ba4cd; background: #dceaf7; }"
      "QComboBox { padding: 1px 2px; font-size: 11px; max-width: 95px; }"
    );
    auto * navLay = new QHBoxLayout(navBar);
    navLay->setContentsMargins(1, 1, 1, 1);
    navLay->setSpacing(2);

    FDriveCombo = new QComboBox;
    FDriveCombo->setToolTip("Select volume / drive / root");
    FDriveCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    FDriveCombo->setMaximumWidth(95);
    FDriveCombo->setStyleSheet("font-size: 11px;");

    FBookmarksBtn = new QToolButton;
    FBookmarksBtn->setIcon(QIcon(":/nav_bookmarks.png"));
    FBookmarksBtn->setToolTip("Directory bookmarks and history (Ctrl+B)");
    FBookmarksBtn->setPopupMode(QToolButton::InstantPopup);

    FFilterBtn = new QToolButton;
    FFilterBtn->setIcon(QIcon(":/nav_filter.png"));
    FFilterBtn->setToolTip("Filter directory contents");
    FFilterBtn->setPopupMode(QToolButton::InstantPopup);

    auto * backBtn = new QToolButton; backBtn->setIcon(QIcon(":/nav_back.png")); backBtn->setToolTip("Back (Alt+Left)");
    auto * fwdBtn  = new QToolButton; fwdBtn->setIcon(QIcon(":/nav_fwd.png"));  fwdBtn->setToolTip("Forward (Alt+Right)");
    auto * upBtn = new QToolButton; upBtn->setIcon(QIcon(":/nav_up.png"));  upBtn->setToolTip("Parent directory (Ctrl+PgUp)");
    auto * rootBtn = new QToolButton; rootBtn->setIcon(QIcon(":/nav_root.png")); rootBtn->setToolTip("Root directory (Ctrl+\\)");
    auto * homeBtn = new QToolButton; homeBtn->setIcon(QIcon(":/nav_home.png")); homeBtn->setToolTip("Home directory (Ctrl+H)");
    auto * rfBtn = new QToolButton; rfBtn->setIcon(QIcon(":/nav_refresh.png")); rfBtn->setToolTip("Refresh (Ctrl+R)");

    FTreeToggleBtn = new QToolButton;
    FTreeToggleBtn->setIcon(QIcon(":/nav_tree.png"));
    FTreeToggleBtn->setToolTip("Toggle directory tree (Ctrl+Alt+T)");
    FTreeToggleBtn->setCheckable(true);

    FPathEdit = new QLineEdit;
    FPathEdit->setStyleSheet("QLineEdit { border: 1px solid #b0b0b0; border-radius: 2px; padding: 2px 4px; font-size: 11px; }");

    navLay->addWidget(FDriveCombo);
    navLay->addWidget(FBookmarksBtn);
    navLay->addWidget(FFilterBtn);
    navLay->addWidget(backBtn);
    navLay->addWidget(fwdBtn);
    navLay->addWidget(upBtn);
    navLay->addWidget(rootBtn);
    navLay->addWidget(homeBtn);
    navLay->addWidget(rfBtn);
    navLay->addWidget(FTreeToggleBtn);
    navLay->addWidget(FPathEdit, 1);

    // 2. Action Toolbar (Upload/Download, Edit, Delete, Properties, New, +, -, Filter)
    auto * cmdBar = new QWidget;
    cmdBar->setStyleSheet(
      "QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 1px 1px; margin: 0px; font-size: 11px; }"
      "QToolButton:hover { border: 1px solid #b8cfe5; background: #eef5fb; }"
      "QToolButton:pressed { border: 1px solid #7ba4cd; background: #dceaf7; }"
    );
    auto * cmdLay = new QHBoxLayout(cmdBar);
    cmdLay->setContentsMargins(0, 0, 0, 0);
    cmdLay->setSpacing(1);

    FActionBtn = new QToolButton;
    FActionBtn->setIcon(QIcon(":/menu_upload.png"));
    FActionBtn->setText("Upload");
    FActionBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    FActionBtn->setPopupMode(QToolButton::InstantPopup);

    FEditBtn = new QToolButton;
    FEditBtn->setIcon(QIcon(":/menu_edit.png"));
    FEditBtn->setText("Edit");
    FEditBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    FEditBtn->setPopupMode(QToolButton::InstantPopup);

    FDeleteBtn = new QToolButton;
    FDeleteBtn->setIcon(QIcon(":/menu_delete.png"));
    FDeleteBtn->setToolTip("Delete (F8)");

    FPropertiesBtn = new QToolButton;
    FPropertiesBtn->setIcon(QIcon(":/menu_properties.png"));
    FPropertiesBtn->setText("Properties");
    FPropertiesBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    FPropertiesBtn->setPopupMode(QToolButton::InstantPopup);

    FNewBtn = new QToolButton;
    FNewBtn->setIcon(QIcon(":/btn_new.png"));
    FNewBtn->setText("New");
    FNewBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    FNewBtn->setPopupMode(QToolButton::InstantPopup);

    auto * sep = new QFrame; sep->setFrameShape(QFrame::VLine); sep->setFrameShadow(QFrame::Sunken);

    FSelectPlusBtn = new QToolButton;
    FSelectPlusBtn->setIcon(QIcon(":/btn_select_plus.png"));
    FSelectPlusBtn->setToolTip("Select files by mask (Ctrl++)");

    FSelectMinusBtn = new QToolButton;
    FSelectMinusBtn->setIcon(QIcon(":/btn_select_minus.png"));
    FSelectMinusBtn->setToolTip("Unselect files by mask (Ctrl+-)");

    FFilterMaskBtn = new QToolButton;
    FFilterMaskBtn->setIcon(QIcon(":/nav_filter.png"));
    FFilterMaskBtn->setText("Filter");
    FFilterMaskBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    FFilterMaskBtn->setToolTip("Filter files in directory");
    FFilterMaskBtn->setStyleSheet("font-size: 11px;");

    cmdLay->addWidget(FActionBtn);
    cmdLay->addWidget(FEditBtn);
    cmdLay->addWidget(FDeleteBtn);
    cmdLay->addWidget(FPropertiesBtn);
    cmdLay->addWidget(FNewBtn);
    cmdLay->addWidget(sep);
    cmdLay->addWidget(FSelectPlusBtn);
    cmdLay->addWidget(FSelectMinusBtn);
    cmdLay->addWidget(FFilterMaskBtn);
    cmdLay->addStretch(1);

    FView = new QTableView;
    FModel = new QStandardItemModel(this);
    FView->setModel(FModel);
    FView->setSelectionBehavior(QAbstractItemView::SelectRows);
    FView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    FView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FView->setSortingEnabled(false);
    FView->setShowGrid(false);
    FView->setAlternatingRowColors(true);
    FView->verticalHeader()->setVisible(false);
    FView->verticalHeader()->setDefaultSectionSize(20);
    FView->installEventFilter(this);
    FView->viewport()->installEventFilter(this);     // drag start + drop target
    FView->viewport()->setAcceptDrops(true);

    FStatus = new QLabel;
    FStatus->setStyleSheet("padding:2px 6px; background:palette(window); border-top:1px solid palette(mid);");

    // Optional directory tree (WinSCP-style), hidden by default; lazy-loaded, click navigates.
    FTree = new QTreeView; FTreeModel = new QStandardItemModel(this);
    FTree->setModel(FTreeModel); FTree->setHeaderHidden(true); FTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FTree->hide();
    QObject::connect(FTree, &QTreeView::expanded, this, [this](const QModelIndex & ix){ treeExpand(ix); });
    QObject::connect(FTree, &QTreeView::clicked, this, [this](const QModelIndex & ix){
      if (FTreePopulating) return;
      QString p = ix.data(Qt::UserRole + 1).toString();
      if (!p.isEmpty() && p != FPath) navigate(p);
    });
    auto * split = new QSplitter(Qt::Horizontal);
    split->addWidget(FTree); split->addWidget(FView);
    split->setStretchFactor(0, 0); split->setStretchFactor(1, 1);
    split->setSizes({ 200, 600 });

    layout->addWidget(FHeader);
    layout->addWidget(navBar);
    layout->addWidget(cmdBar);
    layout->addWidget(split, 1);
    layout->addWidget(FStatus);

    QObject::connect(FView, &QTableView::doubleClicked,
                     [this](const QModelIndex & ix) { activate(ix.row()); });
    QObject::connect(FPathEdit, &QLineEdit::returnPressed,
                     [this] { navigate(FPathEdit->text()); });
    QObject::connect(upBtn, &QToolButton::clicked, [this] { goUp(); });
    QObject::connect(rootBtn, &QToolButton::clicked, [this] { navigate("/"); });
    QObject::connect(rfBtn, &QToolButton::clicked, [this] { refresh(); });
    QObject::connect(backBtn, &QToolButton::clicked, [this] { goBack(); });
    QObject::connect(fwdBtn, &QToolButton::clicked, [this] { goForward(); });
    QObject::connect(homeBtn, &QToolButton::clicked, [this] { if (onHome) onHome(); });
    QObject::connect(FTreeToggleBtn, &QToolButton::toggled, [this](bool v) { setTreeVisible(v); });
    QObject::connect(FDriveCombo, QOverload<int>::of(&QComboBox::activated), [this](int idx) {
      QString p = FDriveCombo->itemData(idx).toString();
      if (!p.isEmpty() && p != FPath) navigate(p);
    });

    // Menus
    auto * actionMenu = new QMenu(FActionBtn);
    actionMenu->addAction(QIcon(":/menu_upload.png"), "Transfer\tF5", [this]{
      if (FRemote) { if (onDownload) onDownload(); } else { if (onUpload) onUpload(); }
    });
    actionMenu->addAction(QIcon(":/tb_queue.png"), "Transfer to Queue", [this]{
      if (FRemote) { if (onDownload) onDownload(); } else { if (onUpload) onUpload(); }
    });
    actionMenu->addSeparator();
    actionMenu->addAction(QIcon(":/menu_move.png"), "Move\tF6", [this]{ if (onMove) onMove(); });
    FActionBtn->setMenu(actionMenu);
    QObject::connect(FActionBtn, &QToolButton::clicked, [this] {
      if (FRemote) { if (onDownload) onDownload(); } else { if (onUpload) onUpload(); }
    });

    auto * editMenu = new QMenu(FEditBtn);
    editMenu->addAction(QIcon(":/menu_edit.png"), "Edit\tF4", [this]{ if (onEdit) onEdit(); });
    editMenu->addAction("Internal Editor\tShift+F4", [this]{ if (onInternalEdit) onInternalEdit(); });
    FEditBtn->setMenu(editMenu);
    QObject::connect(FEditBtn, &QToolButton::clicked, [this]{ if (onEdit) onEdit(); });

    QObject::connect(FDeleteBtn, &QToolButton::clicked, [this]{ if (onDelete) onDelete(); });

    auto * propsMenu = new QMenu(FPropertiesBtn);
    propsMenu->addAction(QIcon(":/menu_properties.png"), "Properties\tF9", [this]{ if (onProperties) onProperties(); });
    propsMenu->addAction("Calculate Directory Sizes\tShift+Alt+Enter", [this]{ if (onCalculateDirSizes) onCalculateDirSizes(); });
    FPropertiesBtn->setMenu(propsMenu);
    QObject::connect(FPropertiesBtn, &QToolButton::clicked, [this]{ if (onProperties) onProperties(); });

    auto * newMenu = new QMenu(FNewBtn);
    newMenu->addAction(QIcon(":/menu_open.png"), "File…\tShift+F4", [this]{ if (onNewFile) onNewFile(); });
    newMenu->addAction(QIcon(":/folder.png"), "Directory…\tF7", [this]{ if (onNewDir) onNewDir(); });
    FNewBtn->setMenu(newMenu);

    QObject::connect(FSelectPlusBtn, &QToolButton::clicked, [this]{ if (onSelectMask) onSelectMask(); });
    QObject::connect(FSelectMinusBtn, &QToolButton::clicked, [this]{ if (onUnselectMask) onUnselectMask(); });

    auto promptFilter = [this] {
      bool ok = false;
      QString m = QInputDialog::getText(this, "Filter Files", "Enter file mask (*.*, *.php, *.png, etc.):", QLineEdit::Normal, FFilterPattern.isEmpty() ? "*.*" : FFilterPattern, &ok);
      if (ok) setFilterMask(m.trimmed());
    };
    QObject::connect(FFilterMaskBtn, &QToolButton::clicked, promptFilter);

    auto * filterMenu = new QMenu(FFilterBtn);
    filterMenu->addAction("All Files (*.*)", [this]{ setFilterMask("*.*"); });
    filterMenu->addAction("Source Code (*.c;*.cpp;*.h;*.py;*.php;*.js;*.html)", [this]{ setFilterMask("*.c;*.cpp;*.h;*.py;*.php;*.js;*.html"); });
    filterMenu->addAction("Images (*.png;*.jpg;*.gif;*.svg)", [this]{ setFilterMask("*.png;*.jpg;*.gif;*.svg"); });
    filterMenu->addSeparator();
    filterMenu->addAction("Custom Filter…", promptFilter);
    FFilterBtn->setMenu(filterMenu);

    auto * bmMenu = new QMenu(FBookmarksBtn);
    QObject::connect(bmMenu, &QMenu::aboutToShow, [this, bmMenu] {
      bmMenu->clear();
      bmMenu->addAction(QIcon(":/btn_new.png"), "Add Bookmark…\tCtrl+B", [this]{ if (onAddBookmark) onAddBookmark(FPath); });
      bmMenu->addAction("Go to Directory…\tCtrl+D", [this]{
        bool ok = false;
        QString p = QInputDialog::getText(this, "Go to Directory", "Path:", QLineEdit::Normal, FPath, &ok);
        if (ok && !p.isEmpty()) navigate(p);
      });
      bmMenu->addSeparator();
      for (int i = qMax(0, FHistory.size() - 8); i < FHistory.size(); ++i) {
        QString h = FHistory[i];
        bmMenu->addAction(QIcon(":/folder.png"), h, [this, h]{ navigate(h); });
      }
    });
    FBookmarksBtn->setMenu(bmMenu);
    QObject::connect(FView->selectionModel(), &QItemSelectionModel::selectionChanged,
                     [this] { if (onStatusChanged) onStatusChanged(); });
    // Columns + header set up ONCE here (not per-render) so they're user-resizable and widths persist.
    FModel->setColumnCount(5);
    FModel->setHorizontalHeaderLabels({ "Name", "Size", "Changed", "Rights", "Owner" });
    auto * hh = FView->horizontalHeader();
    hh->setSectionsClickable(true);
    hh->setSectionsMovable(false);
    hh->setStretchLastSection(true);                 // last column fills leftover space
    for (int c = 0; c < 5; ++c) hh->setSectionResizeMode(c, QHeaderView::Interactive);  // all resizable
    FView->setColumnWidth(0, 240); FView->setColumnWidth(1, 90);
    FView->setColumnWidth(2, 150); FView->setColumnWidth(3, 95);
    QObject::connect(hh, &QHeaderView::sectionClicked, [this](int col) { sortByColumn(col); });
  }

  QString path() const { return FPath; }
  QTableView * view() const { return FView; }
  bool isRemote() const { return FRemote; }
  // Size (bytes) of a listed entry by name, for the transfer queue; 0 if unknown/dir.
  qint64 sizeOf(const QString & name) const {
    std::string n = s8(name);
    for (const auto & e : FEntries) if (e.name == n) return e.isDir ? FDirSizes.value(name, 0) : (qint64)e.size;
    return 0;
  }
  // The current listing (for Compare Directories).
  const std::vector<engine::DirEntry> & entries() const { return FEntries; }
  // A calculated directory size ("Calculate Directory Sizes"): shown in the Size column until the
  // panel moves to another directory. Ignored if the panel is no longer showing `forPath`.
  void setDirSize(const QString & forPath, const QString & name, qint64 bytes) {
    if (forPath != FPath) return;
    FDirSizes.insert(name, bytes);
    for (int r = 0; r < (int)FEntries.size(); ++r)
      if (FEntries[r].isDir && !FEntries[r].isParent && u8(FEntries[r].name) == name && FModel->item(r, 1))
        FModel->item(r, 1)->setText(u8(engine::formatSize(bytes)));
    if (onStatusChanged) onStatusChanged();
  }
  bool hasDirSize(const QString & name) const { return FDirSizes.contains(name); }
  // Replace the selection with exactly these entries; the first one becomes the current row.
  void selectNames(const QStringList & names) {
    QItemSelectionModel * sm = FView->selectionModel(); if (!sm) return;
    sm->clearSelection();
    int first = -1;
    for (int r = 0; r < (int)FEntries.size(); ++r) {
      if (FEntries[r].isParent || !names.contains(u8(FEntries[r].name))) continue;
      sm->select(QItemSelection(FModel->index(r, 0), FModel->index(r, FModel->columnCount() - 1)), QItemSelectionModel::Select);
      if (first < 0) first = r;
    }
    if (first >= 0) { sm->setCurrentIndex(FModel->index(first, 0), QItemSelectionModel::NoUpdate); FView->scrollTo(FModel->index(first, 0)); }
    if (onStatusChanged) onStatusChanged();
  }
  // Entry metadata by name (nullptr if not in the current listing).
  const engine::DirEntry * entryNamed(const QString & name) const {
    std::string n = s8(name);
    for (const auto & e : FEntries) if (e.name == n) return &e;
    return nullptr;
  }
  // Does the current listing already contain this name (for overwrite confirmation)?
  bool contains(const QString & name) const {
    std::string n = s8(name);
    for (const auto & e : FEntries) if (!e.isParent && e.name == n) return true;
    return false;
  }
  void setLocal() {
    FRemote = false;
    FHeader->setText("\xF0\x9F\x92\xBB  Local");
    if (FActionBtn) {
      FActionBtn->setIcon(QIcon(":/menu_upload.png"));
      FActionBtn->setText("Upload");
      FActionBtn->setToolTip("Upload selected files (F5)");
    }
    populateDrives();
  }
  void setRemote(const QString & label) {
    FRemote = true;
    FHeader->setText("\xF0\x9F\x8C\x90  " + label);
    if (FActionBtn) {
      FActionBtn->setIcon(QIcon(":/menu_download.png"));
      FActionBtn->setText("Download");
      FActionBtn->setToolTip("Download selected files (F5)");
    }
    populateDrives();
  }

  void populateDrives() {
    if (!FDriveCombo) return;
    FDriveCombo->blockSignals(true);
    FDriveCombo->clear();
    if (!FRemote) {
      FDriveCombo->addItem(qApp->style()->standardIcon(QStyle::SP_DriveHDIcon), "/", "/");
      FDriveCombo->addItem(QIcon(":/nav_home.png"), "~ (Home)", QDir::homePath());
      QDir volDir("/Volumes");
      if (volDir.exists()) {
        for (const QString & v : volDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
          FDriveCombo->addItem(qApp->style()->standardIcon(QStyle::SP_DriveHDIcon), v, "/Volumes/" + v);
        }
      }
      FDriveCombo->addItem(qApp->style()->standardIcon(QStyle::SP_DirIcon), "Users", "/Users");
      FDriveCombo->addItem(qApp->style()->standardIcon(QStyle::SP_DirIcon), "Applications", "/Applications");
    } else {
      FDriveCombo->addItem(qApp->style()->standardIcon(QStyle::SP_DirIcon), "/", "/");
      if (!FPath.isEmpty()) FDriveCombo->addItem(qApp->style()->standardIcon(QStyle::SP_DirIcon), FPath, FPath);
    }
    FDriveCombo->blockSignals(false);
  }

  void updateDriveCombo() {
    if (!FDriveCombo) return;
    FDriveCombo->blockSignals(true);
    if (!FRemote) {
      int idx = -1;
      for (int i = 0; i < FDriveCombo->count(); ++i) {
        QString p = FDriveCombo->itemData(i).toString();
        if (!p.isEmpty() && FPath.startsWith(p)) {
          if (idx < 0 || p.length() > FDriveCombo->itemData(idx).toString().length())
            idx = i;
        }
      }
      if (idx >= 0) FDriveCombo->setCurrentIndex(idx);
    } else {
      int idx = FDriveCombo->findData(FPath);
      if (idx < 0) {
        FDriveCombo->insertItem(0, qApp->style()->standardIcon(QStyle::SP_DirIcon), FPath, FPath);
        idx = 0;
      }
      FDriveCombo->setCurrentIndex(idx);
    }
    FDriveCombo->blockSignals(false);
  }

  void setFilterMask(const QString & mask) {
    FFilterPattern = mask;
    if (FFilterPattern == "*.*" || FFilterPattern == "*") FFilterPattern.clear();
    refresh();
  }

  void setActive(bool a)
  {
    FActive = a;
    FHeader->setStyleSheet(QString("padding:4px 6px; font-weight:bold; color:%1; background:%2;")
                             .arg(a ? "palette(highlighted-text)" : "palette(window-text)")
                             .arg(a ? "palette(highlight)" : "palette(midlight)"));
  }

  void goBack()    { if (FHistIdx > 0) { FNav = true; navigate(FHistory[--FHistIdx]); FNav = false; } }
  void goForward() { if (FHistIdx + 1 < FHistory.size()) { FNav = true; navigate(FHistory[++FHistIdx]); FNav = false; } }

  void navigate(const QString & path)
  {
    if (FRemote && gTransferRunning.load()) return;   // engine busy with a transfer batch
    if (!FNav)   // record history (truncate any forward entries)
    {
      while (FHistory.size() > FHistIdx + 1) FHistory.removeLast();
      if (FHistory.isEmpty() || FHistory.last() != path) { FHistory.append(path); FHistIdx = FHistory.size() - 1; }
    }
    std::string listError;
    std::vector<engine::DirEntry> entries = FRemote ? engine::listRemoteDir(s8(path), &listError) : engine::listLocalDir(s8(path));
    if (FRemote && !listError.empty())
    {
      // WinSCP reports the error and stays where it was; it never shows an unreadable directory as
      // an empty one. Go back to the directory the panel was showing (the session may already have
      // changed into the unreadable one).
      QString prev = FPath;
      if (!FHistory.isEmpty() && FHistory.last() == path && !FNav)
      { FHistory.removeLast(); FHistIdx = FHistory.size() - 1; }
      if (!prev.isEmpty() && prev != path)
      {
        std::string again;
        std::vector<engine::DirEntry> back = engine::listRemoteDir(s8(prev), &again);
        if (again.empty()) entries = back; else { entries.clear(); }
      }
      else entries.clear();
      FPathEdit->setText(prev);
      if (onListError) onListError(u8(listError));
      if (prev.isEmpty()) FPath = path;      // first listing of the session failed: nothing to go back to
    }
    else
    {
      FPath = path;
      FPathEdit->setText(path);
    }
    if (FPath != FListedPath) FDirSizes.clear();   // calculated sizes belong to the directory they were made for
    FListedPath = FPath;
    FEntries = entries;
    if (!gShowHidden)
      FEntries.erase(std::remove_if(FEntries.begin(), FEntries.end(),
        [](const engine::DirEntry & e) { return !e.isParent && !e.name.empty() && e.name[0] == '.'; }),
        FEntries.end());
    if (!FFilterPattern.isEmpty()) {
      QStringList patterns = FFilterPattern.split(QRegularExpression("[;,]"), Qt::SkipEmptyParts);
      FEntries.erase(std::remove_if(FEntries.begin(), FEntries.end(),
        [&](const engine::DirEntry & e) {
          if (e.isParent || e.isDir) return false;
          QString nm = u8(e.name);
          for (const QString & p : patterns) {
            QRegularExpression re(QRegularExpression::wildcardToRegularExpression(p.trimmed()), QRegularExpression::CaseInsensitiveOption);
            if (re.match(nm).hasMatch()) return false;
          }
          return true;
        }),
        FEntries.end());
    }
    FDirs = 0; FFiles = 0;
    for (const auto & e : FEntries) { if (e.isParent) continue; if (e.isDir) ++FDirs; else ++FFiles; }
    sortEntries();
    renderEntries();
    updateDriveCombo();
    if (onStatusChanged) onStatusChanged();
  }

  // Sort FEntries by the active column ("." parent always first), then dirs-first within the
  // chosen key (WinSCP groups folders), respecting ascending/descending.
  void sortEntries()
  {
    std::stable_sort(FEntries.begin(), FEntries.end(), [this](const engine::DirEntry & a, const engine::DirEntry & b) {
      if (a.isParent != b.isParent) return a.isParent > b.isParent;     // ".." first
      if (a.isDir != b.isDir) return a.isDir > b.isDir;                 // dirs before files
      int c = 0;
      switch (FSortCol)
      {
        case 1: { qint64 sa = a.isDir ? FDirSizes.value(u8(a.name), 0) : (qint64)a.size,   // Size (calculated, for directories)
                         sb = b.isDir ? FDirSizes.value(u8(b.name), 0) : (qint64)b.size;
                  c = (sa < sb) ? -1 : (sa > sb) ? 1 : 0; break; }
        case 2: c = QString::compare(u8(a.modified), u8(b.modified)); break;     // Changed
        case 3: c = QString::compare(u8(a.rights), u8(b.rights)); break;         // Rights
        case 4: c = QString::compare(u8(a.owner), u8(b.owner)); break;           // Owner
        default: c = QString::compare(u8(a.name), u8(b.name), Qt::CaseInsensitive); break;  // Name
      }
      return FSortAsc ? (c < 0) : (c > 0);
    });
  }

  void renderEntries()
  {
    // Remove only the rows (NOT clear()) so the columns, header sections and the user's column
    // widths survive a navigation. Sort arrow goes on the header text via setHeaderData.
    if (FModel->rowCount() > 0) FModel->removeRows(0, FModel->rowCount());
    static const char * heads[5] = { "Name", "Size", "Changed", "Rights", "Owner" };
    for (int i = 0; i < 5; ++i) {
      QString h = heads[i]; if (i == FSortCol) h += FSortAsc ? "  \xE2\x96\xB2" : "  \xE2\x96\xBC";
      FModel->setHeaderData(i, Qt::Horizontal, h);
    }
    for (const auto & e : FEntries)
    {
      QList<QStandardItem *> row;
      auto * nameItem = new QStandardItem(u8(e.name));
      nameItem->setIcon(iconFor(e));               // native folder / file-type icon
      row << nameItem;
      auto * sz = new QStandardItem(e.isDir ? (FDirSizes.contains(u8(e.name)) ? u8(engine::formatSize(FDirSizes.value(u8(e.name)))) : QString())
                                            : u8(engine::formatSize(e.size)));
      sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row << sz;
      row << new QStandardItem(u8(e.modified));
      row << new QStandardItem(u8(e.rights));
      row << new QStandardItem(u8(e.owner));
      FModel->appendRow(row);
    }
    if (FModel->rowCount() > 0) FView->selectRow(0);
  }

  void sortByColumn(int col)
  {
    if (col == FSortCol) FSortAsc = !FSortAsc; else { FSortCol = col; FSortAsc = true; }
    sortEntries();
    renderEntries();
    if (onStatusChanged) onStatusChanged();
  }

  // Per-panel status: selection / totals (WinSCP shows selected of total).
  QString statusText() const
  {
    QStringList sel = selectedItems();
    if (!sel.isEmpty())
    {
      qint64 bytes = 0; for (const QString & n : sel) bytes += sizeOf(n);
      return QString("%1 of %2 selected  \xE2\x80\x94  %3")
        .arg(sel.size()).arg(FFiles + FDirs).arg(u8(engine::formatSize(bytes)));
    }
    return QString("%1 director%2, %3 file(s)").arg(FDirs).arg(FDirs == 1 ? "y" : "ies").arg(FFiles);
  }

  // Selected real files only (excludes dirs + "..").
  QStringList selectedFiles() const { return selected(false); }
  // Selected items including directories (excludes ".."); used by copy/move/delete which the
  // engine handles recursively for folders.
  QStringList selectedItems() const { return selected(true); }

  QStringList selected(bool includeDirs) const
  {
    QStringList out;
    if (!FView->selectionModel()) return out;
    const auto rows = FView->selectionModel()->selectedRows();
    for (const auto & ix : rows)
    {
      int r = ix.row();
      if (r < 0 || r >= (int)FEntries.size() || FEntries[r].isParent) continue;
      if (FEntries[r].isDir && !includeDirs) continue;
      out << u8(FEntries[r].name);
    }
    return out;
  }

  // Select (or deselect) all entries matching a wildcard mask (e.g. "*.txt") — WinSCP Ctrl+Num+/-.
  void selectByMask(const QString & mask, bool sel) {
    QItemSelectionModel * sm = FView->selectionModel(); if (!sm) return;
    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(mask), QRegularExpression::CaseInsensitiveOption);
    for (int r = 0; r < (int)FEntries.size(); ++r) {
      if (FEntries[r].isParent) continue;
      if (re.match(u8(FEntries[r].name)).hasMatch())
        sm->select(QItemSelection(FModel->index(r, 0), FModel->index(r, 4)),
                   sel ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
    }
    if (onStatusChanged) onStatusChanged();
  }
  void setStatusLine(const QString & s) { FStatus->setText(s); }
  // The listing as text (name, size column, selected mark) - for scripted UI checks.
  QStringList dump() const {
    QStringList out;
    QItemSelectionModel * sm = FView->selectionModel();
    for (int r = 0; r < (int)FEntries.size(); ++r) {
      if (FEntries[r].isParent) continue;
      out << QString("%1 %2%3 | %4 | %5").arg(sm && sm->isRowSelected(r, QModelIndex()) ? "*" : " ")
               .arg(u8(FEntries[r].name), FEntries[r].isDir ? "/" : "")
               .arg(FModel->item(r, 1) ? FModel->item(r, 1)->text() : QString())
               .arg(u8(FEntries[r].modified));
    }
    return out;
  }
  // Native icon for an entry: folder icon for dirs, "up" for "..", file-type icon (by extension) for
  // files. Cached per extension so listing stays fast.
  QIcon iconFor(const engine::DirEntry & e) const {
    static QFileIconProvider prov;
    static QIcon folder = prov.icon(QFileIconProvider::Folder);
    static QIcon upIcon = qApp->style()->standardIcon(QStyle::SP_FileDialogToParent);
    static QIcon genericFile = prov.icon(QFileIconProvider::File);
    static QHash<QString, QIcon> byExt;
    if (e.isParent) return upIcon;
    if (e.isDir) return folder;
    QString n = u8(e.name); int dot = n.lastIndexOf('.');
    QString ext = (dot > 0) ? n.mid(dot + 1).toLower() : QString();
    if (ext.isEmpty()) return genericFile;
    auto it = byExt.find(ext);
    if (it != byExt.end()) return it.value();
    QIcon ic = prov.icon(QFileInfo("x." + ext));   // type icon by extension
    if (ic.isNull()) ic = genericFile;
    byExt.insert(ext, ic);
    return ic;
  }
  void refresh() { navigate(FPath); }
  // Norton-style: toggle selection of the current row, then move down one.
  void toggleSelectAndAdvance() {
    int row = FView->currentIndex().row();
    if (row < 0) return;
    QItemSelectionModel * sm = FView->selectionModel();
    QModelIndex ix = FModel->index(row, 0);
    sm->select(QItemSelection(FModel->index(row, 0), FModel->index(row, FModel->columnCount() - 1)),
               QItemSelectionModel::Toggle);
    int next = (row + 1 < FModel->rowCount()) ? row + 1 : row;
    FView->setCurrentIndex(FModel->index(next, 0));
  }
  void goUp() { navigate(u8(engine::parentDir(s8(FPath)))); if (onLeaveDir) onLeaveDir(); }

protected:
  bool eventFilter(QObject * obj, QEvent * ev) override
  {
    if (ev->type() == QEvent::FocusIn && onActivated) onActivated();
    if (ev->type() == QEvent::KeyPress)
    {
      auto * ke = static_cast<QKeyEvent *>(ev);
      if (ke->key() == Qt::Key_Backspace) { goUp(); return true; }
      if (ke->key() == Qt::Key_Tab) { if (onSwitchPanel) onSwitchPanel(); return true; }
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
      { activate(FView->currentIndex().row()); return true; }
    }
    // --- drag & drop between panels ---
    if (obj == FView->viewport())
    {
      static const char * kFmt = "application/x-winscp-files";
      if (ev->type() == QEvent::MouseButtonPress)
      { auto * me = static_cast<QMouseEvent *>(ev); if (me->button() == Qt::LeftButton) FDragStart = me->pos(); }
      else if (ev->type() == QEvent::MouseMove)
      {
        auto * me = static_cast<QMouseEvent *>(ev);
        if ((me->buttons() & Qt::LeftButton) &&
            (me->pos() - FDragStart).manhattanLength() >= QApplication::startDragDistance() &&
            !selectedItems().isEmpty())
        {
          s_dragSource = this;
          auto * mime = new QMimeData; mime->setData(kFmt, "1");   // inter-panel marker
          // Also expose the files as URLs so they can be dropped on Finder/Nautilus (drag OUT).
          // Local: real paths (free). Remote: download the selected files to a temp dir first.
          QList<QUrl> urls;
          QStringList selFiles = selectedFiles();   // files only (dirs not exported this way)
          if (!FRemote) {
            for (const QString & f : selFiles) urls << QUrl::fromLocalFile(FPath + "/" + f);
          } else if (!selFiles.isEmpty() && !gTransferRunning.load()) {
            QString tmp = QDir::tempPath() + "/freescp-dragout"; QDir().mkpath(tmp);
            QGuiApplication::setOverrideCursor(Qt::BusyCursor);
            for (const QString & f : selFiles) {
              std::string err;
              if (engine::downloadFromRemote(engine::joinPath(s8(FPath), s8(f)), s8(tmp), &err, true))
                urls << QUrl::fromLocalFile(tmp + "/" + f);
            }
            QGuiApplication::restoreOverrideCursor();
          }
          if (!urls.isEmpty()) mime->setUrls(urls);
          auto * drag = new QDrag(this); drag->setMimeData(mime);
          drag->exec(Qt::CopyAction);
          s_dragSource = nullptr;
          return true;
        }
      }
      else if (ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove)
      {
        auto * de = static_cast<QDragMoveEvent *>(ev);
        bool internal = de->mimeData()->hasFormat(kFmt) && s_dragSource && s_dragSource != this;
        bool external = de->mimeData()->hasUrls();   // files from Finder/Nautilus
        if (internal || external) { de->acceptProposedAction(); return true; }
      }
      else if (ev->type() == QEvent::Drop)
      {
        auto * de = static_cast<QDropEvent *>(ev);
        if (de->mimeData()->hasFormat(kFmt) && s_dragSource && s_dragSource != this)
        { FilePanel * src = s_dragSource; if (onDropFiles) onDropFiles(src, src->selectedItems()); de->acceptProposedAction(); return true; }
        if (de->mimeData()->hasUrls())   // external (Finder) -> import the local files into this panel
        {
          QStringList paths;
          for (const QUrl & u : de->mimeData()->urls()) if (u.isLocalFile()) paths << u.toLocalFile();
          if (!paths.isEmpty() && onDropExternal) { onDropExternal(this, paths); de->acceptProposedAction(); return true; }
        }
      }
    }
    return false;
  }

private:
  void activate(int row)
  {
    if (row < 0 || row >= static_cast<int>(FEntries.size())) return;
    const auto & e = FEntries[static_cast<size_t>(row)];
    if (!e.isDir) { if (onFileOpen) onFileOpen(); return; }
    if (e.isParent) { goUp(); }
    else { QString nm = u8(e.name); navigate(u8(engine::joinPath(s8(FPath), e.name))); if (onEnterDir) onEnterDir(nm); }
  }

  QLabel * FHeader;
  QComboBox * FDriveCombo = nullptr;
  QToolButton * FBookmarksBtn = nullptr;
  QToolButton * FFilterBtn = nullptr;
  QToolButton * FTreeToggleBtn = nullptr;
  QToolButton * FActionBtn = nullptr;
  QToolButton * FEditBtn = nullptr;
  QToolButton * FDeleteBtn = nullptr;
  QToolButton * FPropertiesBtn = nullptr;
  QToolButton * FNewBtn = nullptr;
  QToolButton * FSelectPlusBtn = nullptr;
  QToolButton * FSelectMinusBtn = nullptr;
  QToolButton * FFilterMaskBtn = nullptr;
  QString FFilterPattern;
  QLineEdit * FPathEdit;
  QTableView * FView;
  QStandardItemModel * FModel;
  QLabel * FStatus;
  QTreeView * FTree = nullptr;
  QStandardItemModel * FTreeModel = nullptr;
  bool FTreePopulating = false;
  QString FPath;
  bool FRemote = false;
  bool FActive = false;
  bool FNav = false;
  int FDirs = 0, FFiles = 0;
  int FSortCol = 0; bool FSortAsc = true;
  QPoint FDragStart;
  QStringList FHistory;
  int FHistIdx = -1;
  std::vector<engine::DirEntry> FEntries;
  QHash<QString, qint64> FDirSizes;   // name -> calculated total size, for FListedPath
  QString FListedPath;
};
FilePanel * FilePanel::s_dragSource = nullptr;

//===========================================================================
// Internal text editor (the classic WinSCP notepad): edit + save, line/col, Find, word-wrap,
// Save & Close, and an unsaved-changes prompt on close. onSave returns true on success.
//===========================================================================
class EditorWindow : public QMainWindow
{
public:
  QPlainTextEdit * te;
  std::function<bool()> onSave;

  EditorWindow(QWidget * parent, const QString & title, const QString & text) : QMainWindow(parent), FBase(title)
  {
    setAttribute(Qt::WA_DeleteOnClose);
    te = new QPlainTextEdit; te->setPlainText(text);
    te->setLineWrapMode(QPlainTextEdit::NoWrap);
    te->setFont(QFont("Menlo", 12));
    te->setTabStopDistance(4 * te->fontMetrics().horizontalAdvance(' '));
    setCentralWidget(te);
    auto * tb = addToolBar("Edit"); tb->setMovable(false);
    auto * aSave = tb->addAction("\xF0\x9F\x92\xBE Save"); aSave->setShortcut(QKeySequence::Save);
    connect(aSave, &QAction::triggered, this, [this]{ doSave(); });
    auto * aSaveClose = tb->addAction("Save && Close");
    connect(aSaveClose, &QAction::triggered, this, [this]{ if (doSave()) close(); });
    tb->addSeparator();
    auto * aFind = tb->addAction("\xF0\x9F\x94\x8D Find"); aFind->setShortcut(QKeySequence::Find);
    connect(aFind, &QAction::triggered, this, [this]{ findText(); });
    auto * aWrap = tb->addAction("Wrap"); aWrap->setCheckable(true);
    connect(aWrap, &QAction::toggled, this, [this](bool w){ te->setLineWrapMode(w ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap); });
    statusBar();
    connect(te, &QPlainTextEdit::cursorPositionChanged, this, [this]{ updateStatus(); });
    connect(te->document(), &QTextDocument::modificationChanged, this, [this](bool){ updateTitle(); });
    updateTitle(); updateStatus();
    resize(760, 580);
  }
  bool doSave() { if (onSave && onSave()) { te->document()->setModified(false); updateTitle(); return true; } return false; }

protected:
  void closeEvent(QCloseEvent * e) override
  {
    if (te->document()->isModified())
    {
      auto r = appMessage(this, "Editor", "Save changes before closing?",
                          QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
      if (r == QMessageBox::Cancel) { e->ignore(); return; }
      if (r == QMessageBox::Save && !doSave()) { e->ignore(); return; }
    }
    e->accept();
  }

private:
  QString FBase, FLastFind;
  void updateTitle() { setWindowTitle((te->document()->isModified() ? "* " : "") + FBase); }
  void updateStatus() { auto c = te->textCursor(); statusBar()->showMessage(QString("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.columnNumber() + 1)); }
  void findText()
  {
    bool ok; QString q = QInputDialog::getText(this, "Find", "Find:", QLineEdit::Normal, FLastFind, &ok);
    if (ok && !q.isEmpty()) { FLastFind = q; if (!te->find(q)) { te->moveCursor(QTextCursor::Start); if (!te->find(q)) statusBar()->showMessage("Not found: " + q); } }
  }
};

//===========================================================================

int main(int argc, char ** argv)
{
  engine::installCrashHandler();   // dump a backtrace to the log on a hard crash (BEFORE anything else)
  QApplication app(argc, argv);

  // Detect Portable Mode (PosixSCP.ini or WinSCP.ini)
  QString portableIni;
  QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
  // In a macOS .app bundle, binary is in PosixSCP.app/Contents/MacOS
  QDir bundleOuterDir(appDir + "/../../..");
  if (bundleOuterDir.exists("PosixSCP.ini")) portableIni = bundleOuterDir.filePath("PosixSCP.ini");
  else if (bundleOuterDir.exists("WinSCP.ini")) portableIni = bundleOuterDir.filePath("WinSCP.ini");
#endif
  if (portableIni.isEmpty()) {
    if (QFile::exists(appDir + "/PosixSCP.ini")) portableIni = appDir + "/PosixSCP.ini";
    else if (QFile::exists(appDir + "/WinSCP.ini")) portableIni = appDir + "/WinSCP.ini";
  }

  if (!portableIni.isEmpty()) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QFileInfo(portableIni).absolutePath());
    fprintf(stderr, "[PosixSCP] Portable mode enabled with INI: %s\n", portableIni.toUtf8().constData());
  }

  if (argc > 2 && QString(argv[1]) == "--import-ini") {
    QString err;
    int n = importWinScpIniFile(QString::fromUtf8(argv[2]), &err);
    fprintf(stderr, "[PosixSCP] Imported %d sites from %s (err: %s)\n", n, argv[2], err.toUtf8().constData());
    return 0;
  }
  if (argc > 1 && QString(argv[1]) == "--test-advanced") {
    engine::ConnectOptions o;
    loadSite("DXN/IndiaOBSParts-AUCloud_India_Test", o);
    showAdvancedDialog(nullptr, o);
    return 0;
  }

  // QSettings scope. Headless/offscreen test runs use an ISOLATED org so they never read or wipe the
  // user's real saved sites/prefs. Every QSettings() default-ctor below inherits this org + app name.
  if (qgetenv("QT_QPA_PLATFORM") == "offscreen") {
    QCoreApplication::setOrganizationName("PosixSCP-test");
  } else if (!portableIni.isEmpty()) {
    QCoreApplication::setOrganizationName("");
  } else {
    QCoreApplication::setOrganizationName("PosixSCP");
  }
  app.setApplicationName("PosixSCP");
  app.setApplicationVersion(POSIXSCP_VERSION);
  app.setApplicationDisplayName("PosixSCP");
  QIcon appIco(":/posixscp.png");
  if (appIco.isNull()) appIco = QIcon(":/winscp.png");
  app.setWindowIcon(appIco);

  // Check for CLI import command: --import-winscp <path>
  for (int i = 1; i < argc; ++i) {
    QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == "--import-winscp" && i + 1 < argc) {
      QString iniPath = QString::fromLocal8Bit(argv[++i]);
      QString err;
      int cnt = importWinScpIniFile(iniPath, &err);
      if (!err.isEmpty()) {
        fprintf(stderr, "[PosixSCP] Import error: %s\n", err.toUtf8().constData());
        return 1;
      }
      fprintf(stderr, "[PosixSCP] Successfully imported %d site(s) and workspaces from %s\n",
              cnt, iniPath.toUtf8().constData());
      return 0;
    }
  }
  { QSettings s;
    gShowHidden = s.value("prefs/showHidden", true).toBool();
    gConfirmDelete = s.value("prefs/confirmDelete", true).toBool();
    gConfirmOverwrite = s.value("prefs/confirmOverwrite", true).toBool();
    gParallelMax = s.value("prefs/parallelMax", 2).toInt();
    gAltColors = s.value("prefs/altColors", true).toBool();
    gCompareByTime = s.value("prefs/compareByTime", true).toBool();
    gCompareBySize = s.value("prefs/compareBySize", false).toBool();
    gUseExternalEditor = s.value("editor/useExternal", false).toBool();
    gExternalEditorPath = s.value("editor/externalPath", "").toString();
    gDoubleClickToEdit = s.value("editor/doubleClickEdit", true).toBool(); }
  engine::setTransferSettings(loadTransferSettings());   // text/binary, timestamps, masks, resume, speed limit

  QMainWindow window;
  // Scripted-UI commands (WINSCP_UITEST); each feature registers the ones that exercise it.
  std::map<QString, std::function<void(const QStringList &)>> uiCmd;
  window.setWindowTitle("PosixSCP");
  window.resize(1100, 720);
  { QSettings s;
    if (s.contains("win/geometry")) window.restoreGeometry(s.value("win/geometry").toByteArray()); }

  // Main toolbar (matching WinSCP Commander layout).
  auto * tb = window.addToolBar("Main");
  tb->setMovable(false);
  tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  tb->setIconSize(QSize(18, 18));

  auto * actSync = tb->addAction(QIcon(":/tb_sync_browse.png"), "");
  actSync->setCheckable(true);
  actSync->setToolTip("Synchronize browsing (mirror directory changes in both panels)");

  auto * actCompareDirs = tb->addAction(QIcon(":/tb_compare_dirs.png"), "");
  actCompareDirs->setToolTip("Compare Directories (Shift+F2)");

  auto * actSynchronize = tb->addAction(QIcon(":/tb_synchronize.png"), "Synchronize");
  actSynchronize->setToolTip("Synchronize directories (Ctrl+S)");

  auto * actConsole = tb->addAction(QIcon(":/tb_console.png"), "");
  actConsole->setToolTip("Open Terminal / Console (Ctrl+T)");

  auto * actPutty = tb->addAction(QIcon(":/tb_terminal_ssh.png"), "");
  actPutty->setToolTip("Open in SSH Terminal (Ctrl+T)");

  auto * actRefresh = tb->addAction(QIcon(":/tb_refresh.png"), "");
  actRefresh->setToolTip("Refresh (Ctrl+R)");

  auto * actPrefsBtn = tb->addAction(QIcon(":/tb_preferences.png"), "");
  actPrefsBtn->setToolTip("Preferences (Ctrl+Alt+P)");

  auto * tbQueue = tb->addAction(QIcon(":/tb_queue.png"), "Queue");
  tbQueue->setToolTip("Transfer Queue (Ctrl+Q)");
  tbQueue->setCheckable(true);

  tb->addSeparator();

  auto * xferWidget = new QWidget;
  auto * xferLay = new QHBoxLayout(xferWidget);
  xferLay->setContentsMargins(4, 0, 4, 0);
  xferLay->setSpacing(6);
  auto * xferLbl = new QLabel("Transfer Settings");
  auto * comboXfer = new QComboBox;
  comboXfer->addItem("Default");
  comboXfer->addItem("Text");
  comboXfer->addItem("Binary");
  comboXfer->addItem("Custom…");
  xferLay->addWidget(xferLbl);
  xferLay->addWidget(comboXfer);
  tb->addWidget(xferWidget);

  tb->addSeparator();

  auto * actGlobe = tb->addAction(QIcon(":/tb_globe.png"), "");
  actGlobe->setToolTip("Session status & reconnect");

  auto * actConnect = new QAction("Login", &window);
  auto * actDisconnect = new QAction("Disconnect", &window);
  actDisconnect->setEnabled(false);
  auto * actHome = new QAction("Home", &window);
  auto * actTree = new QAction("Directory tree", &window); actTree->setCheckable(true);
  auto * tbNewDir = new QAction("New folder", &window);
  auto * tbDelete = new QAction("Delete", &window);
  auto * tbProps = new QAction("Properties", &window);
  auto * tbLog = new QAction("Log", &window); tbLog->setCheckable(true);

  // Keep every command reachable: size the window so the toolbar never collapses into Qt's
  // hard-to-hover overflow popup at normal sizes (WinSCP keeps its toolbar fully visible).
  window.setMinimumWidth(qMax(960, tb->sizeHint().width() + 16));

  // Panels.
  auto * splitter = new QSplitter(Qt::Horizontal);
  auto * left = new FilePanel;
  auto * right = new FilePanel;
  left->setLocal();
  right->setLocal();
  splitter->addWidget(left);
  splitter->addWidget(right);
  { QSettings s;
    if (s.contains("win/splitter")) splitter->restoreState(s.value("win/splitter").toByteArray());
    else splitter->setSizes({ 550, 550 }); }

  auto * central = new QWidget;
  auto * centralLay = new QVBoxLayout(central);
  centralLay->setContentsMargins(0, 0, 0, 0);
  centralLay->setSpacing(0);
  // Session tab bar (matching WinSCP Commander tabs)
  auto * tabBar = new QTabBar;
  tabBar->setTabsClosable(true);
  tabBar->setExpanding(false);
  tabBar->setDrawBase(false);
  tabBar->setDocumentMode(true);          // left-aligns tabs (macOS centers them otherwise)
  tabBar->setStyleSheet(
    "QTabBar::tab {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f5f5f5, stop:1 #dedede);"
    "  border: 1px solid #adadad;"
    "  border-bottom: 1px solid #adadad;"
    "  border-top-left-radius: 3px;"
    "  border-top-right-radius: 3px;"
    "  padding: 3px 10px;"
    "  margin-right: 2px;"
    "  font-size: 11px;"
    "  color: #222222;"
    "}"
    "QTabBar::tab:selected {"
    "  background: #ffffff;"
    "  border-color: #3b82f6;"
    "  border-bottom: 1px solid #ffffff;"
    "  font-weight: bold;"
    "  color: #1e3a8a;"
    "}"
    "QTabBar::tab:hover:!selected {"
    "  background: #fbfbfb;"
    "}"
  );
  tabBar->addTab(QIcon(":/site.png"), "Local");

  auto * tabRow = new QWidget;
  auto * tabRowLay = new QHBoxLayout(tabRow);
  tabRowLay->setContentsMargins(4, 2, 4, 1); tabRowLay->setSpacing(4);
  auto * newTabBtn = new QToolButton;
  newTabBtn->setIcon(QIcon(":/tb_new_tab.png"));
  newTabBtn->setText("New Tab");
  newTabBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  newTabBtn->setPopupMode(QToolButton::InstantPopup);
  newTabBtn->setToolTip("Open new session in a new tab (Ctrl+N)");
  newTabBtn->setStyleSheet(
    "QToolButton {"
    "  border: 1px solid #adadad;"
    "  border-radius: 3px;"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fafafa, stop:1 #e5e5e5);"
    "  padding: 2px 8px;"
    "  font-size: 11px;"
    "}"
    "QToolButton:hover {"
    "  background: #f0f4f8;"
    "  border-color: #3b82f6;"
    "}"
  );
  tabRowLay->addWidget(tabBar); tabRowLay->addWidget(newTabBtn); tabRowLay->addStretch(1);   // tabs + New Tab button to the left
  tabRow->show();
  centralLay->addWidget(tabRow);
  centralLay->addWidget(splitter, 1);

  // Bottom function-key bar (the iconic WinSCP/Norton-Commander row).
  auto * fkeys = new QWidget;
  auto * fkLay = new QHBoxLayout(fkeys);
  fkLay->setContentsMargins(3, 3, 3, 3);
  fkLay->setSpacing(3);
  central->setLayout(centralLay);
  centralLay->addWidget(fkeys);
  window.setCentralWidget(central);

  FilePanel * active = left;
  static QString remoteHome = "/";
  static QString connHost, connUser; static int connPort = 22; static bool connSsh = false;  // for Open Terminal

  auto updateStatuses = [&] {
    left->setStatusLine(left->statusText());
    right->setStatusLine(right->statusText());
    window.statusBar()->showMessage(active->path());
    if (right->isRemote()) {
      QString curLabel = (tabBar->count() > 0) ? tabBar->tabText(tabBar->currentIndex()) : QString();
      if (curLabel.isEmpty()) curLabel = QString("%1@%2").arg(connUser, connHost);
      window.setWindowTitle(QString("%1 \xE2\x80\x93 %2 \xE2\x80\x93 PosixSCP").arg(right->path(), curLabel));
    } else {
      window.setWindowTitle(QString("%1 \xE2\x80\x93 PosixSCP").arg(active->path()));
    }
  };
  left->onStatusChanged = updateStatuses;
  right->onStatusChanged = updateStatuses;
  left->onActivated = [&] { active = left; left->setActive(true); right->setActive(false); updateStatuses(); };
  right->onActivated = [&] { active = right; right->setActive(true); left->setActive(false); updateStatuses(); };
  left->onHome  = [&] { left->navigate(u8(engine::homeDir())); };
  right->onHome = [&] { right->navigate(right->isRemote() ? remoteHome : u8(engine::homeDir())); };
  // Synchronized browsing: entering/leaving a dir in one panel mirrors to the other.
  left->onEnterDir  = [&](const QString & n) { if (actSync->isChecked()) right->enterSubdir(n); };
  left->onLeaveDir  = [&] { if (actSync->isChecked()) right->upOne(); };
  right->onEnterDir = [&](const QString & n) { if (actSync->isChecked()) left->enterSubdir(n); };
  right->onLeaveDir = [&] { if (actSync->isChecked()) left->upOne(); };
  QObject::connect(actTree, &QAction::toggled, [&](bool v){ left->setTreeVisible(v); right->setTreeVisible(v); });
  left->onSwitchPanel  = [&] { right->view()->setFocus(); };
  right->onSwitchPanel = [&] { left->view()->setFocus(); };

  // Session log dock (see what the engine does — invaluable for testing).
  auto * logDock = new QDockWidget("Session log", &window);
  auto * logView = new QPlainTextEdit; logView->setReadOnly(true);
  logView->setMaximumBlockCount(2000);
  logDock->setWidget(logView);
  window.addDockWidget(Qt::BottomDockWidgetArea, logDock);
  logDock->hide();
  auto log = [&](const QString & s) { logView->appendPlainText(s); engine::logLine("[ui] " + s8(s)); };

  // Transfer queue dock (WinSCP-style): a list of transfers with live progress. Processed
  // sequentially on the UI thread (processEvents keeps it responsive) — visible like WinSCP's queue.
  auto * queueDock = new QDockWidget("Transfer queue", &window);
  auto * queueWidget = new QWidget;
  auto * queueLay = new QVBoxLayout(queueWidget);
  queueLay->setContentsMargins(0, 0, 0, 0); queueLay->setSpacing(2);
  auto * queueBtns = new QHBoxLayout; queueBtns->setContentsMargins(3, 2, 3, 0);
  auto * btnCancel = new QPushButton("Cancel"); btnCancel->setEnabled(false);
  auto * btnClear = new QPushButton("Clear finished");
  queueBtns->addWidget(btnCancel); queueBtns->addWidget(btnClear); queueBtns->addStretch(1);
  auto * queueView = new QTableView;
  auto * queueModel = new QStandardItemModel(0, 7, &window);
  queueModel->setHorizontalHeaderLabels({ "Operation", "File", "Size", "Progress", "Speed", "Time left", "Status" });
  queueView->setModel(queueModel);
  queueView->verticalHeader()->setVisible(false);
  queueView->verticalHeader()->setDefaultSectionSize(20);   // compact rows (match the panels)
  queueView->setShowGrid(false);
  queueView->setAlternatingRowColors(true);
  queueView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  queueView->horizontalHeader()->setStretchLastSection(true);
  queueView->setColumnWidth(1, 200);
  queueLay->addLayout(queueBtns); queueLay->addWidget(queueView);
  queueDock->setWidget(queueWidget);
  window.addDockWidget(Qt::BottomDockWidgetArea, queueDock);
  queueDock->hide();

  auto fmtSpeed = [](qint64 cps) -> QString { return cps > 0 ? u8(engine::formatSize(cps)) + "/s" : QString(); };
  auto fmtEta = [](qint64 secs) -> QString {
    if (secs <= 0) return QString();
    return QString::asprintf("%lld:%02lld", (long long)(secs / 60), (long long)(secs % 60));
  };
  auto queueAdd = [&](const QString & op, const QString & file, qint64 size) -> int {
    QList<QStandardItem *> row;
    row << new QStandardItem(op) << new QStandardItem(file)
        << new QStandardItem(size > 0 ? u8(engine::formatSize(size)) : QString())
        << new QStandardItem("0%") << new QStandardItem() << new QStandardItem() << new QStandardItem("queued");
    queueModel->appendRow(row);
    return queueModel->rowCount() - 1;
  };
  auto queueCell = [&](int r, int c, const QString & v) { if (r >= 0 && r < queueModel->rowCount()) queueModel->item(r, c)->setText(v); };
  auto queueStatus = [&](int r, const QString & s) { queueCell(r, 6, s); };

  // Single-file upload that shows in the transfer queue (used by editor save + edit-and-watch).
  // Runs on a worker so the UI stays responsive; refreshes the remote panel on success.
  auto enqueueUpload = [&](const QString & localPath, const QString & remoteDir, const QString & opName) {
    queueDock->show();
    int row = queueAdd(opName, QFileInfo(localPath).fileName(), QFileInfo(localPath).size());
    queueStatus(row, "active");
    std::string lp = s8(localPath), rd = s8(remoteDir);
    std::thread([&, row, lp, rd]{
      std::string err; bool ok = engine::uploadToRemote(lp, rd, &err, true);   // editor save: this exact file, as is
      QMetaObject::invokeMethod(&window, [&, row, ok, err]{
        queueStatus(row, ok ? "done" : ("failed: " + u8(err)));
        if (ok) { queueCell(row, 3, "100%"); window.statusBar()->showMessage("Uploaded " + queueModel->item(row, 1)->text()); if (right->isRemote()) right->refresh(); }
        else window.statusBar()->showMessage("Upload failed: " + u8(err));
      }, Qt::QueuedConnection);
    }).detach();
  };

  // Cancel flag for the currently-running batch (Cancel button -> sink returns true -> engine aborts).
  auto currentCancel = std::make_shared<std::shared_ptr<std::atomic<bool>>>();
  QObject::connect(btnCancel, &QPushButton::clicked, [&, currentCancel]{
    if (*currentCancel) (*currentCancel)->store(true);
    window.statusBar()->showMessage("Cancelling\xE2\x80\xA6");
  });
  QObject::connect(btnClear, &QPushButton::clicked, [&]{
    for (int r = queueModel->rowCount() - 1; r >= 0; --r) {
      QString st = queueModel->item(r, 6)->text();
      if (st == "done" || st.startsWith("failed") || st.startsWith("cancelled")) queueModel->removeRow(r);
    }
  });

  auto busy = [&]() -> bool {
    if (gTransferRunning.load()) { window.statusBar()->showMessage("Transfer in progress\xE2\x80\xA6 please wait"); return true; }
    return false;
  };

  // Background transfer: runs the batch on a worker thread (UI never freezes); the queue dock updates
  // live with %/speed/time-left (marshaled to the UI thread). Cancel aborts via the engine progress
  // sink. Single-connection/non-thread-safe engine -> serialized by the bridge mutex + gTransferRunning
  // gating remote UI actions for the batch (local browsing stays responsive).
  // Overwrite confirmation: returns the names to transfer (all, or non-existing on Skip) or empty on
  // Cancel. Honors the Preferences "Confirm overwrites" toggle.
  auto confirmOverwrite = [&](FilePanel * dest, QStringList files) -> QStringList {
    if (!gConfirmOverwrite) return files;
    QStringList existing; for (const QString & f : files) if (dest->contains(f)) existing << f;
    if (existing.isEmpty()) return files;
    QMessageBox box(&window); box.setIcon(QMessageBox::Question); box.setWindowTitle("Confirm overwrite");
    box.setText(QString("%1 of %2 item(s) already exist in %3.").arg(existing.size()).arg(files.size()).arg(dest->path()));
    if (existing.size() <= 10) box.setInformativeText(existing.join("\n"));
    auto * over = box.addButton("Overwrite", QMessageBox::AcceptRole);
    auto * skip = box.addButton("Skip existing", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == over) return files;
    if (box.clickedButton() == skip) { QStringList keep; for (const QString & f : files) if (!dest->contains(f)) keep << f; return keep; }
    return QStringList();
  };

  auto startTransfer = [&](FilePanel * from, FilePanel * to, QStringList files, bool isMove, bool background = false) {
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (from->isRemote() && to->isRemote())
    { appMessage(&window, "Transfer", "Remote-to-remote is not supported yet."); return; }
    if (busy()) return;
    files = confirmOverwrite(to, files);
    if (files.isEmpty()) { window.statusBar()->showMessage("Transfer cancelled"); return; }
    const bool srcRemote = from->isRemote(), dstRemote = to->isRemote();
    const std::string srcDir = s8(from->path()), dstDir = s8(to->path());
    const QString op = srcRemote ? "Download" : (dstRemote ? "Upload" : "Copy");
    std::vector<int> rows; std::vector<std::string> names;
    for (const QString & f : files) { rows.push_back(queueAdd(isMove ? "Move" : op, f, from->sizeOf(f))); names.push_back(s8(f)); }
    gTransferRunning = true;
    btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    *currentCancel = cancel;

    auto progressDlg = new TransferProgressDialog(&window, isMove ? "Moving" : (srcRemote ? "Downloading" : (dstRemote ? "Uploading" : "Copying")),
                                                  (int)names.size(), cancel);
    QObject::connect(progressDlg, &TransferProgressDialog::backgroundRequested, [&]{
      queueDock->show();
    });
    if (background) {
      queueDock->show();
    } else {
      if (!uiScripted()) progressDlg->show();
    }

    // Parallelism: a plain remote copy of >1 files uses up to 2 extra connections (each its own
    // worker). Moves, local copies and single files stay serial. Falls back to serial if the pool
    // can't open. Per-worker progress -> that worker's current queue row.
    const bool wantParallel = (srcRemote != dstRemote) && !isMove && names.size() > 1 && engine::parallelSupported();
    std::vector<int> handles;
    if (wantParallel)
    {
      int want = (int)std::min<size_t>((size_t)std::max(1, gParallelMax), names.size());
      for (int k = 0; k < want; ++k) { int h = engine::openParallelConnection(nullptr); if (h > 0) handles.push_back(h); }
      if (handles.size() < 2) { engine::closeParallelConnections(); handles.clear(); }  // need >=2 to be worth it
    }
    const int nWorkers = handles.empty() ? 1 : (int)handles.size();

    auto markActive = [&, rows](int rowVal) { QMetaObject::invokeMethod(&window, [&, rowVal]{ queueStatus(rowVal, "active"); }, Qt::QueuedConnection); };
    auto markDone = [&](int rowVal, const QString & status, bool okRow) {
      QMetaObject::invokeMethod(&window, [&, rowVal, status, okRow]{
        queueStatus(rowVal, status); if (okRow) { queueCell(rowVal, 3, "100%"); queueCell(rowVal, 4, QString()); queueCell(rowVal, 5, QString()); }
      }, Qt::QueuedConnection);
    };
    auto progressFor = [&, cancel, progressDlg](int rowVal, int fileIndex, const engine::TransferProgress & tp) -> bool {
      int pct = tp.total > 0 ? (int)(tp.transferred * 100 / tp.total) : 0;
      qint64 cps = tp.cps; qint64 eta = (cps > 0 && tp.total > tp.transferred) ? (tp.total - tp.transferred) / cps : 0;
      QMetaObject::invokeMethod(&window, [&, rowVal, pct, cps, eta]{
        queueCell(rowVal, 3, QString("%1%").arg(pct)); queueCell(rowVal, 4, fmtSpeed(cps)); queueCell(rowVal, 5, fmtEta(eta));
      }, Qt::QueuedConnection);
      QMetaObject::invokeMethod(progressDlg, [progressDlg, tp, fileIndex]{
        progressDlg->updateProgress(tp, fileIndex);
      }, Qt::QueuedConnection);
      return cancel->load();
    };

    auto nextIdx = std::make_shared<std::atomic<size_t>>(0);
    auto okCount = std::make_shared<std::atomic<int>>(0);

    // Coordinator thread: spawn workers, join, then marshal completion (keeps the UI thread free).
    std::thread([&, rows, names, srcDir, dstDir, srcRemote, dstRemote, isMove, from, to, cancel, handles, nWorkers, nextIdx, okCount, markActive, markDone, progressFor, progressDlg]{
      auto worker = [&](int handle) {   // handle 0 = primary (serial), >=1 = pool connection
        auto curRow = std::make_shared<std::atomic<int>>(-1);
        auto curIdx = std::make_shared<std::atomic<int>>(1);
        if (handle == 0) engine::setProgressSink([&, curRow, curIdx](const engine::TransferProgress & tp){ return progressFor(curRow->load(), curIdx->load(), tp); });
        else engine::setProgressSinkVia(handle, [&, curRow, curIdx](const engine::TransferProgress & tp){ return progressFor(curRow->load(), curIdx->load(), tp); });
        for (;;)
        {
          size_t i = nextIdx->fetch_add(1);
          if (i >= names.size()) break;
          int rowVal = rows[i];
          if (cancel->load()) { markDone(rowVal, "cancelled", false); continue; }
          curRow->store(rowVal);
          curIdx->store((int)i + 1);
          markActive(rowVal);
          std::string src = engine::joinPath(srcDir, names[i]);
          std::string curName = names[i];
          QMetaObject::invokeMethod(progressDlg, [progressDlg, curName, dstDir, i]{
            progressDlg->setCurrentFile(u8(curName), u8(dstDir), (int)i + 1);
          }, Qt::QueuedConnection);
          std::string err; bool r;
          if (!srcRemote && !dstRemote) r = engine::copyFile(src, engine::joinPath(dstDir, names[i]));
          else if (handle != 0) r = srcRemote ? engine::downloadVia(handle, src, dstDir, &err) : engine::uploadVia(handle, src, dstDir, &err);
          else r = srcRemote ? engine::downloadFromRemote(src, dstDir, &err) : engine::uploadToRemote(src, dstDir, &err);
          if (r && isMove) r = srcRemote ? engine::remoteDelete(names[i], &err) : engine::localDelete(srcDir, names[i], &err);
          bool cancelled = cancel->load();
          markDone(rowVal, cancelled ? "cancelled" : (r ? "done" : ("failed" + (err.empty() ? QString() : (": " + u8(err))))), r && !cancelled);
          if (r && !cancelled) okCount->fetch_add(1);
        }
      };
      std::vector<std::thread> ts;
      if (handles.empty()) worker(0);
      else { for (int h : handles) ts.emplace_back([&, h]{ worker(h); }); for (auto & t : ts) t.join(); }

      int ok = okCount->load(), total = (int)names.size();
      QMetaObject::invokeMethod(&window, [&, ok, total, isMove, from, to, progressDlg]{
        progressDlg->finishTransfer();
        progressDlg->close();
        progressDlg->deleteLater();
        engine::setProgressSink(nullptr);
        engine::closeParallelConnections();
        gTransferRunning = false;
        btnCancel->setEnabled(false);
        to->refresh(); if (isMove) from->refresh();
        window.statusBar()->showMessage(QString("Transferred %1/%2 item(s)").arg(ok).arg(total));
        if (ok < total) appMessage(&window, "Transfer",
          QString("%1 of %2 item(s) failed. See the Transfer queue for the error.").arg(total - ok).arg(total));
      }, Qt::QueuedConnection);
    }).detach();
  };

  // Drag files from one panel, drop on the other -> transfer (copy).
  left->onDropFiles  = [&](FilePanel * src, const QStringList & f) { startTransfer(src, left, f, false); };
  right->onDropFiles = [&](FilePanel * src, const QStringList & f) { startTransfer(src, right, f, false); };

  // External drop (Finder/Nautilus) of local files -> upload (remote panel) or copy (local panel).
  auto importFiles = [&](FilePanel * dest, const QStringList & absPathsIn) {
    if (busy()) return;
    const bool toRemote = dest->isRemote();
    const std::string destDir = s8(dest->path());
    // overwrite confirmation by basename
    QStringList baseNames; for (const QString & p : absPathsIn) baseNames << QFileInfo(p).fileName();
    QStringList keepNames = confirmOverwrite(dest, baseNames);
    if (keepNames.isEmpty()) { window.statusBar()->showMessage("Import cancelled"); return; }
    QStringList absPaths; for (const QString & p : absPathsIn) if (keepNames.contains(QFileInfo(p).fileName())) absPaths << p;
    queueDock->show();
    std::vector<int> rows; std::vector<std::string> srcs, nm;
    for (const QString & p : absPaths) {
      QFileInfo fi(p);                                    // files AND folders (engine recurses dirs)
      rows.push_back(queueAdd(toRemote ? "Upload" : "Copy", fi.fileName(), fi.isDir() ? 0 : fi.size()));
      srcs.push_back(s8(p)); nm.push_back(s8(fi.fileName()));
    }
    if (rows.empty()) return;
    gTransferRunning = true; btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false); *currentCancel = cancel;

    auto progressDlg = new TransferProgressDialog(&window, toRemote ? "Uploading" : "Copying",
                                                  (int)srcs.size(), cancel);
    QObject::connect(progressDlg, &TransferProgressDialog::backgroundRequested, [&]{
      queueDock->show();
    });
    if (!uiScripted()) progressDlg->show();

    auto curRow = std::make_shared<std::atomic<int>>(rows[0]);
    auto curIdx = std::make_shared<std::atomic<int>>(1);
    engine::setProgressSink([&, cancel, curRow, curIdx, progressDlg](const engine::TransferProgress & tp) -> bool {
      int rowVal = curRow->load(), pct = tp.total > 0 ? (int)(tp.transferred * 100 / tp.total) : 0;
      qint64 cps = tp.cps, eta = (cps > 0 && tp.total > tp.transferred) ? (tp.total - tp.transferred) / cps : 0;
      QMetaObject::invokeMethod(&window, [&, rowVal, pct, cps, eta]{ queueCell(rowVal, 3, QString("%1%").arg(pct)); queueCell(rowVal, 4, fmtSpeed(cps)); queueCell(rowVal, 5, fmtEta(eta)); }, Qt::QueuedConnection);
      QMetaObject::invokeMethod(progressDlg, [progressDlg, tp, idx = curIdx->load()]{
        progressDlg->updateProgress(tp, idx);
      }, Qt::QueuedConnection);
      return cancel->load();
    });
    std::thread([&, rows, srcs, nm, destDir, toRemote, dest, cancel, curRow, curIdx, progressDlg]{
      int ok = 0;
      for (size_t i = 0; i < srcs.size(); ++i) {
        if (cancel->load()) { QMetaObject::invokeMethod(&window, [&, r = rows[i]]{ queueStatus(r, "cancelled"); }, Qt::QueuedConnection); continue; }
        int rowVal = rows[i]; curRow->store(rowVal); curIdx->store((int)i + 1);
        QMetaObject::invokeMethod(&window, [&, rowVal]{ queueStatus(rowVal, "active"); }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(progressDlg, [progressDlg, curName = nm[i], destDir, i]{
          progressDlg->setCurrentFile(u8(curName), u8(destDir), (int)i + 1);
        }, Qt::QueuedConnection);
        std::string err; bool r = toRemote ? engine::uploadToRemote(srcs[i], destDir, &err)
                                            : engine::copyFile(srcs[i], engine::joinPath(destDir, nm[i]));
        QString status = r ? "done" : ("failed" + (err.empty() ? QString() : (": " + u8(err))));
        QMetaObject::invokeMethod(&window, [&, rowVal, status, r]{ queueStatus(rowVal, status); if (r) queueCell(rowVal, 3, "100%"); }, Qt::QueuedConnection);
        if (r) ++ok;
      }
      int total = (int)srcs.size();
      QMetaObject::invokeMethod(&window, [&, ok, total, dest, progressDlg]{
        progressDlg->finishTransfer();
        progressDlg->close();
        progressDlg->deleteLater();
        engine::setProgressSink(nullptr); gTransferRunning = false; btnCancel->setEnabled(false);
        dest->refresh(); window.statusBar()->showMessage(QString("Imported %1/%2 item(s)").arg(ok).arg(total));
        if (ok < total) appMessage(&window, "Import",
          QString("%1 of %2 item(s) failed. See the Transfer queue for the error.").arg(total - ok).arg(total));
      }, Qt::QueuedConnection);
    }).detach();
  };
  left->onDropExternal = importFiles; right->onDropExternal = importFiles;

  // A remote directory that cannot be opened is an error the user sees (WinSCP does the same),
  // not an empty panel.
  auto listError = [&](const QString & msg) {
    window.statusBar()->showMessage("Error listing directory");
    log("Error listing directory: " + msg);
    appMessage(&window, "Error", msg);
  };
  left->onListError = listError; right->onListError = listError;

  // Edit-and-watch: a remote file opened in an (external) editor is downloaded to temp and watched;
  // saving re-uploads it automatically — the classic WinSCP "edit remote in your editor" magic.
  auto * watcher = new QFileSystemWatcher(&window);
  auto * watchRemoteDir = new QHash<QString, QString>;   // localTempPath -> remote dir
  auto reupload = [&, watcher, watchRemoteDir](const QString & localPath) {
    auto it = watchRemoteDir->find(localPath);
    if (it == watchRemoteDir->end()) return;
    enqueueUpload(localPath, it.value(), "Edit-upload");   // shows in the transfer queue
    if (QFileInfo::exists(localPath) && !watcher->files().contains(localPath)) watcher->addPath(localPath);  // editors that save-by-rename drop the watch
  };
  QObject::connect(watcher, &QFileSystemWatcher::fileChanged, &window, [reupload](const QString & p) { reupload(p); });

  // Host-key verification: show the engine's message (fingerprint) + Yes/No instead of auto-accepting.
  engine::setConfirmCallback([&](const std::string & msg) -> bool {
    if (qgetenv("QT_QPA_PLATFORM") == "offscreen") return true;   // headless tests: auto-accept
    return appMessage(&window, "Verify server identity", u8(msg),   // SSH host key or TLS certificate
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
  });

  // Authentication prompts the stored credentials could not answer: a missing or rejected password,
  // a key passphrase, keyboard-interactive / two-factor challenges, proxy credentials.
  engine::setPromptCallback([&](const engine::PromptRequest & rq, std::vector<std::string> & answers) -> bool {
    return showAuthPrompt(&window, rq, answers);
  });

  //--- session tabs -------------------------------------------------------
  static int curTab = -1;        // previously-active tab (for save-on-switch)
  static bool tabSwitching = false;
  // After a successful connect: add a tab for the new (already-active) bridge session.
  auto addSessionTab = [&](const QString & label) {
    tabSwitching = true;
    int i = -1;
    if (tabBar->count() == 1 && tabBar->tabText(0) == "Local") {
      tabBar->setTabText(0, label);
      i = 0;
    } else {
      i = tabBar->addTab(QIcon(":/site.png"), label);
    }
    tabBar->setTabData(i, QStringList{ right->path(), left->path() });   // [remote, local]
    tabBar->setCurrentIndex(i);
    curTab = i;
    tabRow->setVisible(true);
    tabSwitching = false;
  };
  // Switch panels to reflect tab i (assumes engine session i is/should be active).
  auto activateTab = [&](int i) {
    engine::switchSession(i);
    right->setRemote(tabBar->tabText(i));
    QStringList st = tabBar->tabData(i).toStringList();
    if (st.size() >= 1 && !st[0].isEmpty()) right->navigate(st[0]);
    if (st.size() >= 2 && !st[1].isEmpty()) left->navigate(st[1]);
    curTab = i;
    if (right->isRemote())
      window.setWindowTitle(QString("%1 \xE2\x80\x93 %2 \xE2\x80\x93 PosixSCP").arg(right->path(), tabBar->tabText(i)));
  };
  QObject::connect(tabBar, &QTabBar::currentChanged, [&](int i) {
    if (tabSwitching || i < 0) return;
    if (curTab >= 0 && curTab < tabBar->count())                         // save outgoing tab's paths
      tabBar->setTabData(curTab, QStringList{ right->path(), left->path() });
    tabSwitching = true; activateTab(i); tabSwitching = false;
  });
  auto closeTab = [&](int i) {
    if (busy() || i < 0 || i >= tabBar->count()) return;
    if (tabBar->count() == 1 && tabBar->tabText(0) == "Local") {
      right->navigate(u8(engine::homeDir()));
      return;
    }
    tabSwitching = true;
    engine::closeSession(i);
    tabBar->removeTab(i);
    if (tabBar->count() == 0) {
      tabBar->addTab(QIcon(":/site.png"), "Local");
      curTab = 0;
      right->setLocal(); right->navigate(u8(engine::homeDir()));
      actDisconnect->setEnabled(false); window.setWindowTitle("PosixSCP");
    } else {
      int ni = qMin(i, tabBar->count() - 1);
      tabBar->setCurrentIndex(ni); activateTab(ni);
    }
    tabSwitching = false;
  };
  QObject::connect(tabBar, &QTabBar::tabCloseRequested, [&](int i) { closeTab(i); });

  //--- operations ---------------------------------------------------------
  // After a successful connect (interactive or scripted): show the session in the remote panel.
  static engine::ConnectOptions connOpts;   // the active session's options (Open Terminal, copy URL)
  auto sessionOpened = [&](const engine::ConnectOptions & o, const engine::ConnectResult & r, const QString & siteName = QString()) {
    QString tabLabel = siteName.isEmpty() ? QString("%1@%2").arg(u8(o.user), u8(o.host)) : siteName;
    right->setRemote(tabLabel + " \xE2\x80\x94 " + protocolName(o.protocol));
    remoteHome = u8(r.currentDir);
    connOpts = o;
    connHost = u8(o.host); connUser = u8(o.user); connPort = o.port;
    connSsh = (o.protocol == engine::Protocol::Sftp || o.protocol == engine::Protocol::Scp);

    // Target remote directory if configured
    QString targetRemote = u8(r.currentDir);
    if (!o.remoteDir.empty()) {
      targetRemote = u8(o.remoteDir);
    }
    right->navigate(targetRemote);
    right->onActivated();

    // Target local directory if configured
    if (!o.localDir.empty()) {
      QString resolvedLoc = resolveLocalPath(u8(o.localDir));
      if (!resolvedLoc.isEmpty() && QDir(resolvedLoc).exists()) {
        left->navigate(resolvedLoc);
      }
    }

    // Synchronize browsing
    if (o.syncBrowsing) {
      actSync->setChecked(true);
      window.statusBar()->showMessage("Synchronized browsing enabled", 3000);
    }

    actDisconnect->setEnabled(true);
    window.setWindowTitle(QString("%1 \xE2\x80\x93 %2 \xE2\x80\x93 PosixSCP").arg(targetRemote, tabLabel));
    QString route = routeSummary(o);
    log("Connected" + (route.isEmpty() ? QString() : " (" + route + ")") + ". Remote directory: " + targetRemote);
    addSessionTab(tabLabel);   // open a tab for this session
  };
  auto connectWith = [&](const engine::ConnectOptions & o, const QString & siteName = QString()) -> bool {
    window.statusBar()->showMessage("Connecting\xE2\x80\xA6");
    QString route = routeSummary(o);
    log(QString("Connecting to %1:%2%3 \xE2\x80\xA6").arg(u8(o.host)).arg(o.port).arg(route.isEmpty() ? QString() : " via " + route));

    ConnectingDialog dlg(&window, siteName, o);
    if (!uiScripted()) {
      dlg.show();
      QApplication::processEvents();
    }

    engine::setInformationCallback([&dlg](const std::string & text) {
      dlg.addStep(QString::fromUtf8(text.c_str()));
    });

    // Remember where the current tab is before another session takes over the panels (it used to
    // come back at the directory it was opened in, not the one the user had navigated to).
    if (curTab >= 0 && curTab < tabBar->count() && right->isRemote())
      tabBar->setTabData(curTab, QStringList{ right->path(), left->path() });
    engine::ConnectResult r = engine::connect(o);
    engine::setInformationCallback(nullptr);
    dlg.close();
    if (!r.ok)
    {
      log("FAILED: " + u8(r.error));
      logDock->show();
      appMessage(&window, "Login", u8(r.error));
      window.statusBar()->showMessage("Connection failed");
      return false;
    }
    sessionOpened(o, r, siteName);
    return true;
  };
  auto doConnect = [&] {
    if (busy()) return;
    LoginParams lp = showLoginDialog(&window);
    if (!lp.ok) return;
    if (!lp.workspaceSites.isEmpty()) {
      for (const QString & sName : lp.workspaceSites) {
        engine::ConnectOptions wo;
        if (loadSite(sName, wo)) {
          connectWith(wo, sName);
        }
      }
      return;
    }
    if (lp.o.host.empty()) { appMessage(&window, "Login", "Please enter a host name."); return; }
    connectWith(lp.o, lp.siteName);
  };
  auto doDisconnect = [&] {
    if (busy()) return;
    if (tabBar->count() > 0) { closeTab(tabBar->currentIndex()); window.statusBar()->showMessage("Disconnected"); log("Disconnected."); return; }
    engine::disconnectSftp();
    right->setLocal();
    right->navigate(u8(engine::homeDir()));
    actDisconnect->setEnabled(false);
    window.setWindowTitle("PosixSCP");
    window.statusBar()->showMessage("Disconnected");
    log("Disconnected.");
  };

  // Open a system terminal with an ssh command to the current server (the "open in PuTTY" of Windows).
  auto doOpenTerminal = [&] {
    if (!engine::remoteConnected() || !connSsh)
    { window.statusBar()->showMessage("Open Terminal needs an active SFTP/SCP session"); return; }
    // A tunnelled session: the target is only reachable through the jump host (ssh -J).
    QString jump = (connOpts.tunnel && !connOpts.tunnelHost.empty())
      ? QString("-J %1%2:%3 ").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort)
      : QString();
    QString sshCmd = QString("ssh %1%2@%3 -p %4").arg(jump, connUser, connHost).arg(connPort);
#ifdef Q_OS_MACOS
    QString script = QString("tell application \"Terminal\"\nactivate\ndo script \"%1\"\nend tell").arg(sshCmd);
    QProcess::startDetached("osascript", { "-e", script });
#else
    // Linux: try the common terminal emulators with an ssh command.
    for (const QString & term : { QString("x-terminal-emulator"), QString("gnome-terminal"), QString("konsole"), QString("xterm") })
      if (QStandardPaths::findExecutable(term).size())
      { QProcess::startDetached(term, term == "gnome-terminal" ? QStringList{ "--", "bash", "-c", sshCmd + "; exec bash" } : QStringList{ "-e", sshCmd }); break; }
#endif
    window.statusBar()->showMessage("Opened terminal: " + sshCmd);
  };

  // Transfer settings (WinSCP's Transfer Settings dialog): they apply to every transfer and
  // synchronization from now on and are remembered.
  auto applyTransferSettings = [&](const engine::TransferSettings & x) {
    engine::setTransferSettings(x);
    saveTransferSettings(x);
    window.statusBar()->showMessage("Transfer settings: " + transferSummary(x));
  };
  auto editTransferSettings = [&](QWidget * parent) -> bool {
    engine::TransferSettings x = engine::transferSettings();
    if (!showTransferSettingsDialog(parent, x)) return false;
    applyTransferSettings(x);
    return true;
  };

  // WinSCP-style Copy/Move dialog: shows the count + an editable target directory. Returns the target
  // (empty = cancelled). If the user changes the target, the dest panel is navigated there first so
  // the transfer + the post-transfer refresh both target it.
  auto showCopyDialog = [&](const QString & op, int count, const QString & defTarget) -> QString {
    QDialog d(&window); d.setWindowTitle(op);
    auto * lay = new QVBoxLayout(&d);
    lay->addWidget(new QLabel(QString("%1 %2 item(s) to:").arg(op).arg(count)));
    auto * edit = new QLineEdit(defTarget); edit->setMinimumWidth(380); edit->selectAll();
    lay->addWidget(edit);
    // How the files will be transferred, and a way to change it right here (as in WinSCP).
    auto * xferRow = new QHBoxLayout;
    auto * xferLabel = new QLabel(transferSummary(engine::transferSettings()));
    xferLabel->setStyleSheet("color: gray;"); xferLabel->setWordWrap(true);
    auto * xferBtn = new QPushButton("Transfer settings\xE2\x80\xA6");
    xferRow->addWidget(xferLabel, 1); xferRow->addWidget(xferBtn);
    lay->addLayout(xferRow);
    QObject::connect(xferBtn, &QPushButton::clicked, &d, [&] {
      if (editTransferSettings(&d)) xferLabel->setText(transferSummary(engine::transferSettings()));
    });
    auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(op);
    lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    edit->setFocus();
    if (!qEnvironmentVariableIsEmpty("WINSCP_COPY_TARGET")) edit->setText(qEnvironmentVariable("WINSCP_COPY_TARGET"));   // scripted runs
    return (execDialog(d, "copy") == QDialog::Accepted) ? edit->text() : QString();
  };
  auto doCopy = [&] {
    FilePanel * dst = (active == left) ? right : left;
    QStringList files = active->selectedItems();
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (active->isRemote() && dst->isRemote())
    { appMessage(&window, "Copy", "Remote-to-remote copy is not supported yet."); return; }
    if (busy()) return;
    QString target = showCopyDialog("Copy", files.size(), dst->path());
    if (target.isEmpty()) return;
    if (target != dst->path())
    {
      dst->navigate(target);
      // the typed directory could not be opened (the error was shown): do NOT transfer into
      // whatever directory the panel fell back to
      if (dst->isRemote() && dst->path() != target) { window.statusBar()->showMessage("Copy cancelled: cannot open " + target); return; }
    }
    startTransfer(active, dst, files, false);
  };
  auto doMove = [&] {
    FilePanel * dst = (active == left) ? right : left;
    QStringList files = active->selectedItems();
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (active->isRemote() && dst->isRemote())
    { appMessage(&window, "Move", "Remote-to-remote move is not supported yet."); return; }
    if (busy()) return;
    QString target = showCopyDialog("Move", files.size(), dst->path());
    if (target.isEmpty()) return;
    if (target != dst->path())
    {
      dst->navigate(target);
      // the typed directory could not be opened (the error was shown): do NOT transfer into
      // whatever directory the panel fell back to
      if (dst->isRemote() && dst->path() != target) { window.statusBar()->showMessage("Move cancelled: cannot open " + target); return; }
    }
    startTransfer(active, dst, files, true);
  };
  auto doMkdir = [&] {
    if (busy()) return;
    bool okIn = false;
    QString name = QInputDialog::getText(&window, "Create folder", "New folder name:", QLineEdit::Normal, "", &okIn);
    if (!okIn || name.isEmpty()) return;
    std::string err;
    bool ok = active->isRemote() ? engine::remoteMakeDir(s8(name), &err)
                                 : engine::localMakeDir(s8(active->path()), s8(name), &err);
    active->refresh();
    if (ok) { window.statusBar()->showMessage("Created " + name); return; }
    // WinSCP surfaces a failed folder creation as an error dialog (e.g. permission denied),
    // not a silent status-bar line.
    QString detail = u8(err).trimmed();
    window.statusBar()->showMessage("Create folder failed");
    appMessage(&window, "Create folder",
               QString("Error creating folder \"%1\".\n\n%2")
                   .arg(name, detail.isEmpty() ? QStringLiteral("The operation failed.") : detail),
               QMessageBox::Ok);
  };

  // Select files by wildcard mask (Ctrl+Num+) / deselect (Ctrl+Num-).
  auto doSelectMask = [&](bool sel) {
    bool okIn = false;
    QString mask = QInputDialog::getText(&window, sel ? "Select files" : "Unselect files",
                                         "File mask (e.g. *.txt):", QLineEdit::Normal, "*", &okIn);
    if (okIn && !mask.isEmpty()) active->selectByMask(mask, sel);
  };

  // Create a new empty file (Shift+F4) — WinSCP's "New > File".
  auto doNewFile = [&] {
    if (active->isRemote() && busy()) return;
    bool okIn = false;
    QString name = QInputDialog::getText(&window, "Create file", "New file name:", QLineEdit::Normal, "", &okIn);
    if (!okIn || name.isEmpty()) return;
    bool ok; std::string err;
    if (active->isRemote()) {
      QString tmp = QDir::tempPath() + "/winscp-new/" + name; QDir().mkpath(QDir::tempPath() + "/winscp-new");
      { QFile f(tmp); f.open(QIODevice::WriteOnly); f.close(); }
      ok = engine::uploadToRemote(s8(tmp), s8(active->path()), &err, true);
    } else {
      QFile f(active->path() + "/" + name); ok = f.open(QIODevice::WriteOnly); if (ok) f.close(); else err = "cannot create";
    }
    active->refresh();
    window.statusBar()->showMessage(ok ? "Created " + name : "Create file failed — " + u8(err));
  };

  // Copy the selected entries' full path(s) to the clipboard (and, for remote, an sftp:// URL).
  auto doCopyPath = [&] {
    QStringList sel = active->selectedItems(); if (sel.isEmpty()) return;
    QStringList paths;
    for (const QString & f : sel) {
      QString full = active->path() + (active->path().endsWith('/') ? "" : "/") + f;
      if (active->isRemote() && connSsh && !connHost.isEmpty())
        paths << QString("sftp://%1@%2:%3%4").arg(connUser, connHost).arg(connPort).arg(full);
      else paths << full;
    }
    QApplication::clipboard()->setText(paths.join("\n"));
    window.statusBar()->showMessage(QString("Copied %1 path(s) to clipboard").arg(paths.size()));
  };
  auto doRename = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select exactly one item to rename"); return; }
    bool okIn = false;
    QString nn = QInputDialog::getText(&window, "Rename", "New name:", QLineEdit::Normal, sel.first(), &okIn);
    if (!okIn || nn.isEmpty() || nn == sel.first()) return;
    std::string err;
    bool ok = active->isRemote() ? engine::remoteRename(s8(sel.first()), s8(nn), &err)
                                 : engine::localRename(s8(active->path()), s8(sel.first()), s8(nn), &err);
    active->refresh();
    window.statusBar()->showMessage(ok ? "Renamed to " + nn : "Rename failed — " + u8(err));
  };
  auto doDelete = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (gConfirmDelete && appMessage(&window, "Delete", QString("Delete %1 item(s)?").arg(sel.size()),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    engine::logLine("[ui] doDelete: " + std::to_string(sel.size()) + " item(s) remote=" + (active->isRemote() ? "1" : "0") + " in '" + s8(active->path()) + "'");
    int ok = 0; std::string err, lastErr;
    for (const QString & f : sel) {
      bool r = active->isRemote() ? engine::remoteDelete(s8(f), &err) : engine::localDelete(s8(active->path()), s8(f), &err);
      if (r) ++ok; else lastErr = err;
    }
    engine::logLine("[ui] doDelete done: " + std::to_string(ok) + "/" + std::to_string(sel.size()) + (lastErr.empty() ? "" : (" lastErr=" + lastErr)));
    active->refresh();
    window.statusBar()->showMessage(QString("Deleted %1/%2 item(s)").arg(ok).arg(sel.size()));
    if (ok < sel.size()) appMessage(&window, "Delete", "Some items could not be deleted:\n" + u8(lastErr));
  };
  auto doProps = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select exactly one item"); return; }
    if (!active->isRemote()) { appMessage(&window, "Properties", "Properties (permissions) apply to remote files."); return; }
    QString name = sel.first();
    std::string cur = engine::remoteFileOctal(s8(name));
    const engine::DirEntry * e = active->entryNamed(name);
    QString info;
    if (e) {
      info = QString("Type: %1\nSize: %2\nOwner: %3\nModified: %4")
        .arg(e->isDir ? "Directory" : "File")
        .arg(e->isDir ? QString("—") : u8(engine::formatSize(e->size)))
        .arg(e->owner.empty() ? QString("—") : u8(e->owner))
        .arg(e->modified.empty() ? QString("—") : u8(e->modified));
    }
    bool recursive = false;
    QString oct = showPropertiesDialog(&window, name, cur.empty() ? "644" : u8(cur),
                                       info, e && e->isDir, &recursive);
    if (oct.isEmpty()) return;
    std::string err;
    bool ok = engine::remoteChmod(s8(name), s8(oct), recursive, &err);
    active->refresh();
    window.statusBar()->showMessage(ok ? (recursive ? "Set " + oct + " recursively" : "Set " + oct)
                                       : "chmod failed — " + u8(err));
  };
  auto doOpen = [&] {
    if (active->isRemote() && busy()) return;
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select one file to open"); return; }
    QString f = sel.first();
    QString localPath;
    if (active->isRemote())
    {
      QString tmp = QDir::tempPath() + "/winscp-open";
      QDir().mkpath(tmp);
      std::string err;
      window.statusBar()->showMessage("Opening " + f + "\xE2\x80\xA6");
      QApplication::processEvents();
      if (!engine::downloadFromRemote(engine::joinPath(s8(active->path()), s8(f)), s8(tmp), &err, true))
      { appMessage(&window, "Open", "Could not download: " + u8(err)); return; }
      localPath = tmp + "/" + f;
      // watch the temp copy: any save (this app's editor or an external one) re-uploads it
      (*watchRemoteDir)[localPath] = active->path();
      if (!watcher->files().contains(localPath)) watcher->addPath(localPath);
    }
    else localPath = active->path() + "/" + f;
    QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    window.statusBar()->showMessage(active->isRemote() ? ("Opened " + f + " (auto-uploads on save)") : ("Opened " + f));
  };
  // Detected installed editors on the system.
  auto detectInstalledEditors = []() -> QList<QPair<QString, QString>> {
    QList<QPair<QString, QString>> eds;
#ifdef Q_OS_MACOS
    QStringList candidates = {
      "/Applications/Visual Studio Code.app",
      "/Applications/Cursor.app",
      "/Applications/Sublime Text.app",
      "/Applications/BBEdit.app",
      "/Applications/Zed.app",
      "/Applications/kate.app",
      "/Applications/OpenCode.app",
      "/System/Applications/TextEdit.app"
    };
    for (const QString & p : candidates) {
      if (QFile::exists(p)) eds.append({ QFileInfo(p).completeBaseName(), p });
    }
#else
    for (const QString & b : { "code", "cursor", "subl", "gedit", "kate", "kwrite", "xed", "leafpad", "nano", "vim" }) {
      QString full = QStandardPaths::findExecutable(b);
      if (!full.isEmpty()) eds.append({ b, full });
    }
#endif
    return eds;
  };

  // Open with configured editor: handles both internal editor and external editors (e.g. VS Code, Cursor)
  // For remote files, downloads to temporary cache, registers with watcher for auto re-upload upon saving.
  auto openWithEditor = [&](FilePanel * panel, const QString & customApp = QString(), bool forceInternal = false) {
    if (panel->isRemote() && busy()) return;
    QStringList sel = panel->selectedFiles();
    if (sel.isEmpty()) sel = panel->selectedItems();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select one file to edit"); return; }
    QString f = sel.first();

    bool remote = panel->isRemote();
    QString remoteDir = panel->path();
    QString localPath;
    if (remote)
    {
      QString tmp = QDir::tempPath() + "/posixscp-edit"; QDir().mkpath(tmp);
      std::string err;
      window.statusBar()->showMessage("Downloading " + f + " for editing\xE2\x80\xA6");
      QApplication::processEvents();
      if (!engine::downloadFromRemote(engine::joinPath(s8(remoteDir), s8(f)), s8(tmp), &err, true))
      { appMessage(&window, "Edit", "Could not download: " + u8(err)); return; }
      localPath = tmp + "/" + f;
      // watch the temp copy: any save (internal or external editor) re-uploads it automatically
      (*watchRemoteDir)[localPath] = remoteDir;
      if (!watcher->files().contains(localPath)) watcher->addPath(localPath);
    }
    else localPath = panel->path() + "/" + f;

    QString app = customApp.trimmed();
    bool useExternal = false;
    if (forceInternal) {
      useExternal = false;
    } else if (!app.isEmpty()) {
      useExternal = true;
    } else {
      useExternal = gUseExternalEditor && !gExternalEditorPath.trimmed().isEmpty();
      app = gExternalEditorPath.trimmed();
    }

    if (useExternal && !app.isEmpty())
    {
      bool launched = false;
#ifdef Q_OS_MACOS
      if (app.endsWith(".app") || !app.startsWith("/"))
        launched = QProcess::startDetached("/usr/bin/open", QStringList() << "-a" << app << localPath);
      else
        launched = QProcess::startDetached(app, QStringList() << localPath);
#else
      launched = QProcess::startDetached(app, QStringList() << localPath);
#endif
      if (!launched)
      {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(localPath)))
          appMessage(&window, "Editor", "Could not launch editor: " + app);
      }
      window.statusBar()->showMessage(remote ? ("Opened " + f + " in " + QFileInfo(app).completeBaseName() + " (auto-uploads on save)")
                                             : ("Opened " + f + " in " + QFileInfo(app).completeBaseName()));
    }
    else
    {
      QFile file(localPath);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { appMessage(&window, "Edit", "Cannot open file"); return; }
      QString text = QString::fromUtf8(file.readAll()); file.close();

      auto * ed = new EditorWindow(&window, f + (remote ? "  \xE2\x80\x94  " + remoteDir + " (remote)" : ""), text);
      auto * te = ed->te;
      ed->onSave = [te, localPath, remote, remoteDir, f, &window, &left, enqueueUpload]() -> bool {
        QFile out(localPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) { appMessage(&window, "Save", "Cannot write file"); return false; }
        out.write(te->toPlainText().toUtf8()); out.close();
        if (remote) enqueueUpload(localPath, remoteDir, "Edit-upload");   // shows in the transfer queue
        else { window.statusBar()->showMessage("Saved " + f); left->refresh(); }
        return true;
      };
      ed->show();
    }
  };

  auto openWithConfiguredEditor = [&](FilePanel * panel) { openWithEditor(panel, QString(), false); };

  auto doEdit = [&] { openWithConfiguredEditor(active); };
  auto doQuit = [&] { window.close(); };

  // Synchronize directories (local panel <-> remote panel) via the engine's TSynchronizeChecklist.
  auto doSync = [&] {
    FilePanel * lp = left->isRemote() ? right : left;
    FilePanel * rp = left->isRemote() ? left : right;
    if (!rp->isRemote()) { appMessage(&window, "Synchronize", "Connect a remote session first."); return; }
    if (busy()) return;
    QDialog dlg(&window); dlg.setWindowTitle("Synchronize"); dlg.resize(600, 420);
    auto * v = new QVBoxLayout(&dlg);
    auto * form = new QFormLayout;
    form->addRow("Local:", new QLabel(lp->path()));
    form->addRow("Remote:", new QLabel(rp->path()));
    auto * dir = new QComboBox; dir->addItems({ "Local \xE2\x86\x92 Remote (upload)", "Remote \xE2\x86\x92 Local (download)", "Both directions" });
    form->addRow("Direction:", dir);
    auto * cbDel = new QCheckBox("Delete files missing on the source");
    form->addRow("", cbDel);
    v->addLayout(form);
    auto * list = new QTableView; auto * lm = new QStandardItemModel(0, 3, &dlg);
    lm->setHorizontalHeaderLabels({ "Action", "File", "Size" }); list->setModel(lm);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers); list->verticalHeader()->setVisible(false);
    list->horizontalHeader()->setStretchLastSection(true); list->setColumnWidth(1, 320);
    v->addWidget(list, 1);
    auto * bb = new QDialogButtonBox; auto * bCompare = bb->addButton("Compare", QDialogButtonBox::ActionRole);
    auto * bApply = bb->addButton("Synchronize", QDialogButtonBox::AcceptRole); bApply->setEnabled(false);
    bb->addButton(QDialogButtonBox::Close);
    v->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(bCompare, &QPushButton::clicked, [&]{
      lm->removeRows(0, lm->rowCount());
      std::string err;
      auto items = engine::synchronizeCollect(s8(lp->path()), s8(rp->path()), dir->currentIndex(), cbDel->isChecked(), &err);
      if (!err.empty()) { appMessage(&dlg, "Synchronize", u8(err)); return; }
      for (const auto & it : items) {
        QList<QStandardItem *> row;
        row << new QStandardItem(u8(it.action)) << new QStandardItem(u8(it.name))
            << new QStandardItem(it.isDir ? QString() : u8(engine::formatSize(it.size)));
        lm->appendRow(row);
      }
      bApply->setEnabled(lm->rowCount() > 0);
      dlg.setWindowTitle(QString("Synchronize \xE2\x80\x94 %1 change(s)").arg(lm->rowCount()));
    });
    QObject::connect(bApply, &QPushButton::clicked, [&]{
      std::string err;
      bool ok = engine::synchronizeApply(&err);
      engine::synchronizeRelease();
      if (!ok) appMessage(&dlg, "Synchronize", u8(err));
      dlg.accept();
    });
    dlg.exec();
    engine::synchronizeRelease();
    lp->refresh(); rp->refresh();
  };

  //--- Find Files --------------------------------------------------------------
  auto doFind = [&](const QString & autoMask) {
    FilePanel * panel = active;
    FindContext ctx;
    ctx.remote = panel->isRemote();
    ctx.startDir = panel->path();
    ctx.acquire = [&]() -> bool { if (busy()) return false; gTransferRunning = true; return true; };
    ctx.release = [&] { gTransferRunning = false; };
    ctx.focus = [&, panel](const QString & dir, const QString & name) {
      panel->navigate(dir);
      panel->selectNames(QStringList(name));
      panel->view()->setFocus();
    };
    showFindDialog(&window, ctx, autoMask);
  };

  //--- Calculate Directory Sizes ---------------------------------------------
  // The selected directories (or every directory of the panel when none is selected); the totals
  // appear in the Size column. Runs in the background; the queue's Cancel button stops it.
  auto doCalcSizes = [&] {
    FilePanel * panel = active;
    QStringList dirs;
    for (const QString & n : panel->selectedItems()) { const engine::DirEntry * e = panel->entryNamed(n); if (e && e->isDir) dirs << n; }
    if (dirs.isEmpty())
      for (const engine::DirEntry & e : panel->entries()) if (e.isDir && !e.isParent) dirs << u8(e.name);
    if (dirs.isEmpty()) { window.statusBar()->showMessage("No directories here"); return; }
    if (busy()) return;
    gTransferRunning = true;
    btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    *currentCancel = cancel;
    const bool remote = panel->isRemote();
    const QString path = panel->path();
    window.statusBar()->showMessage(QString("Calculating the size of %1 director%2\xE2\x80\xA6").arg(dirs.size()).arg(dirs.size() == 1 ? "y" : "ies"));
    std::thread([&, panel, dirs, remote, path, cancel] {
      qint64 total = 0; int files = 0, done = 0; std::string lastErr;
      for (const QString & name : dirs)
      {
        if (cancel->load()) break;
        engine::SizeResult r; std::string err;
        auto cancelled = [cancel]() { return cancel->load(); };
        bool ok = remote ? engine::remoteCalculateSize(s8(name), r, cancelled, &err)
                         : engine::localCalculateSize(engine::joinPath(s8(path), s8(name)), r, cancelled, &err);
        if (cancel->load()) break;
        if (!ok && !err.empty()) lastErr = err;
        total += r.bytes; files += r.files; ++done;
        const qint64 bytes = r.bytes; const int n = done, of = (int)dirs.size();
        QMetaObject::invokeMethod(&window, [&, panel, path, name, bytes, n, of] {
          panel->setDirSize(path, name, bytes);
          window.statusBar()->showMessage(QString("Calculating sizes\xE2\x80\xA6 %1/%2").arg(n).arg(of));
        }, Qt::QueuedConnection);
      }
      const bool wasCancelled = cancel->load();
      QMetaObject::invokeMethod(&window, [&, total, files, done, wasCancelled, lastErr] {
        gTransferRunning = false;
        btnCancel->setEnabled(false);
        window.statusBar()->showMessage(QString("%1 director%2: %3 in %4 file(s)%5")
          .arg(done).arg(done == 1 ? "y" : "ies").arg(u8(engine::formatSize(total))).arg(files).arg(wasCancelled ? " (cancelled)" : ""));
        if (!lastErr.empty()) appMessage(&window, "Calculate Directory Sizes", "Some directories could not be read:\n" + u8(lastErr));
      }, Qt::QueuedConnection);
    }).detach();
  };

  //--- Compare Directories ------------------------------------------------------
  // Marks, in both panels, the files that are missing or (by the chosen criteria) different on
  // the other side - WinSCP's Commands > Compare Directories.
  auto doCompareDirs = [&] {
    CompareCriteria c; c.byTime = gCompareByTime; c.bySize = gCompareBySize;
    CompareResult r = compareDirectories(left->entries(), right->entries(), c);
    QStringList l, rr;
    for (const std::string & n : r.left) l << u8(n);
    for (const std::string & n : r.right) rr << u8(n);
    left->selectNames(l); right->selectNames(rr);
    const QString how = c.byTime && c.bySize ? "time and size" : c.byTime ? "time" : c.bySize ? "size" : "existence only";
    if (l.isEmpty() && rr.isEmpty())
    {
      window.statusBar()->showMessage("Compare directories: no differences (by " + how + ")");
      appMessage(&window, "Compare Directories", "No differences found (compared by " + how + ").");
    }
    else
      window.statusBar()->showMessage(QString("Compare directories (by %1): %2 file(s) marked on the left, %3 on the right")
                                        .arg(how).arg(l.size()).arg(rr.size()));
  };

  //--- Compare Files (side by side) --------------------------------------------
  // The selected file against the file of the same name in the other panel; or, when the other
  // panel has no such file, against the single file selected there.
  static int compareSeq = 0;
  auto doCompareFiles = [&]() -> QString {
    FilePanel * other = (active == left) ? right : left;
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { QString m = "Select one file to compare."; window.statusBar()->showMessage(m); return m; }
    QString mine = sel.first(), theirs;
    const engine::DirEntry * same = other->entryNamed(mine);
    if (same != nullptr && !same->isDir) theirs = mine;
    else if (other->selectedFiles().size() == 1) theirs = other->selectedFiles().first();
    else
    {
      QString m = QString("\"%1\" has no counterpart in the other panel. Open the directory that holds the other copy, "
                          "or select the file to compare it with there.").arg(mine);
      appMessage(&window, "Compare Files", m);
      return m;
    }
    if ((active->isRemote() || other->isRemote()) && busy()) return QString();
    // the file of the LEFT panel goes on the left
    FilePanel * lpn = left, * rpn = right;
    const QString lname = (active == left) ? mine : theirs, rname = (active == left) ? theirs : mine;
    auto materialize = [&](FilePanel * pn, const QString & name, const char * sideTag, DiffSide & side) -> bool {
      side.title = QString("%1 \xE2\x80\x94 %2 (%3)").arg(name, pn->path(), pn->isRemote() ? "remote" : "local");
      if (!pn->isRemote()) { side.path = pn->path() + (pn->path().endsWith('/') ? "" : "/") + name; return true; }
      const QString tmp = QString("%1/freescp-compare/%2-%3-%4").arg(QDir::tempPath()).arg(QCoreApplication::applicationPid()).arg(++compareSeq).arg(sideTag);
      QDir().mkpath(tmp);
      window.statusBar()->showMessage("Downloading " + name + " to compare\xE2\x80\xA6");
      QApplication::processEvents();
      // downloadFromRemote looks the file up in the session's current listing = this panel's
      std::string err;
      if (!engine::downloadFromRemote(engine::joinPath(s8(pn->path()), s8(name)), s8(tmp), &err, true))
      { appMessage(&window, "Compare Files", "Could not download " + name + ":\n" + u8(err)); return false; }
      side.path = tmp + "/" + name;
      return true;
    };
    DiffSide ls, rs;
    if (!materialize(lpn, lname, "l", ls) || !materialize(rpn, rname, "r", rs)) return QString();
    QString result = showFileComparison(&window, ls, rs);
    window.statusBar()->showMessage(result);
    for (const DiffSide * sd : { &ls, &rs })       // remove the temporary copies
      if (sd->path.startsWith(QDir::tempPath() + "/freescp-compare/")) QDir(QFileInfo(sd->path).absolutePath()).removeRecursively();
    return result;
  };

  std::function<bool(const QString &, const QString &)> copyDirRecursively = [&](const QString & s, const QString & d) -> bool {
    QDir().mkpath(d);
    for (const QString & entry : QDir(s).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
      copyDirRecursively(s + "/" + entry, d + "/" + entry);
    for (const QString & entry : QDir(s).entryList(QDir::Files))
      QFile::copy(s + "/" + entry, d + "/" + entry);
    return true;
  };

  auto doDuplicate = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select one item to duplicate"); return; }
    QString orig = sel.first();
    QFileInfo fi(orig);
    QString defName;
    if (fi.suffix().isEmpty()) defName = orig + ".copy";
    else defName = fi.completeBaseName() + ".copy." + fi.suffix();

    bool okIn = false;
    QString newName = QInputDialog::getText(&window, "Duplicate", "Duplicate '" + orig + "' to:", QLineEdit::Normal, defName, &okIn);
    if (!okIn || newName.isEmpty() || newName == orig) return;

    if (active->isRemote()) {
      if (connSsh && !connHost.isEmpty()) {
        QString remoteDir = active->path();
        QString jump = (connOpts.tunnel && !connOpts.tunnelHost.empty())
          ? QString("-J %1%2:%3 ").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort)
          : QString();
        QString cpCmd = QString("cd \"%1\" && cp -p -R \"%2\" \"%3\"").arg(remoteDir, orig, newName);
        QStringList args;
        if (!jump.isEmpty()) {
          args << "-J" << QString("%1%2:%3").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort);
        }
        args << "-p" << QString::number(connPort) << QString("%1@%2").arg(connUser, connHost) << cpCmd;
        int res = QProcess::execute("ssh", args);
        if (res == 0) {
          active->refresh();
          window.statusBar()->showMessage("Duplicated to " + newName);
          return;
        }
      }
      QString tmpDir = QDir::tempPath() + "/posixscp-dup";
      QDir().mkpath(tmpDir);
      std::string err;
      if (engine::downloadFromRemote(engine::joinPath(s8(active->path()), s8(orig)), s8(tmpDir), &err, true)) {
        QString localOrig = tmpDir + "/" + orig;
        QString localDup = tmpDir + "/" + newName;
        QFile::remove(localDup);
        QFile::copy(localOrig, localDup);
        engine::uploadToRemote(s8(localDup), s8(active->path()), &err, true);
        QFile::remove(localOrig);
        QFile::remove(localDup);
      }
      active->refresh();
      window.statusBar()->showMessage("Duplicated to " + newName);
    } else {
      QString src = active->path() + "/" + orig;
      QString dst = active->path() + "/" + newName;
      bool ok = false;
      if (QFileInfo(src).isDir()) {
        ok = copyDirRecursively(src, dst);
      } else {
        ok = QFile::copy(src, dst);
      }
      active->refresh();
      window.statusBar()->showMessage(ok ? ("Duplicated to " + newName) : "Duplicate failed");
    }
  };

  auto doMoveTo = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) return;
    bool okIn = false;
    QString def = sel.first();
    QString target = QInputDialog::getText(&window, "Move To", QString("Move %1 item(s) to:").arg(sel.size()), QLineEdit::Normal, def, &okIn);
    if (!okIn || target.isEmpty()) return;
    if (sel.size() == 1 && target == sel.first()) return;

    if (sel.size() == 1 && !target.contains('/') && !target.startsWith('.')) {
      std::string err;
      bool ok = active->isRemote() ? engine::remoteRename(s8(sel.first()), s8(target), &err)
                                   : engine::localRename(s8(active->path()), s8(sel.first()), s8(target), &err);
      active->refresh();
      window.statusBar()->showMessage(ok ? "Moved to " + target : "Move failed — " + u8(err));
      return;
    }

    if (active->isRemote()) {
      if (connSsh && !connHost.isEmpty()) {
        QString remoteDir = active->path();
        QString jump = (connOpts.tunnel && !connOpts.tunnelHost.empty())
          ? QString("-J %1%2:%3 ").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort)
          : QString();
        QString escapedItems;
        for (const QString & f : sel) escapedItems += " \"" + f + "\"";
        QString mvCmd = QString("cd \"%1\" && mv%2 \"%3\"").arg(remoteDir, escapedItems, target);
        QStringList args;
        if (!jump.isEmpty()) {
          args << "-J" << QString("%1%2:%3").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort);
        }
        args << "-p" << QString::number(connPort) << QString("%1@%2").arg(connUser, connHost) << mvCmd;
        int res = QProcess::execute("ssh", args);
        active->refresh();
        window.statusBar()->showMessage(res == 0 ? QString("Moved %1 item(s)").arg(sel.size()) : "Move failed");
      }
    } else {
      int moved = 0;
      QString dstBase = target.startsWith('/') ? target : (active->path() + "/" + target);
      for (const QString & f : sel) {
        QString s = active->path() + "/" + f;
        QString d = (QFileInfo(dstBase).isDir()) ? (dstBase + "/" + f) : dstBase;
        if (QFile::rename(s, d)) ++moved;
      }
      active->refresh();
      window.statusBar()->showMessage(QString("Moved %1/%2 item(s)").arg(moved).arg(sel.size()));
    }
  };

  auto doCopyClipboard = [&] {
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) return;
    auto * mimeData = new QMimeData;
    QList<QUrl> urls;
    QStringList paths;
    for (const QString & f : sel) {
      QString full = active->path() + (active->path().endsWith('/') ? "" : "/") + f;
      paths << full;
      if (active->isRemote()) {
        if (connSsh && !connHost.isEmpty())
          urls << QUrl(QString("sftp://%1@%2:%3%4").arg(connUser, connHost).arg(connPort).arg(full));
      } else {
        urls << QUrl::fromLocalFile(full);
      }
    }
    mimeData->setUrls(urls);
    mimeData->setText(paths.join("\n"));
    QApplication::clipboard()->setMimeData(mimeData);
    window.statusBar()->showMessage(QString("Copied %1 item(s) to clipboard").arg(sel.size()));
  };

  auto doCopyNamesClipboard = [&] {
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) return;
    QApplication::clipboard()->setText(sel.join("\n"));
    window.statusBar()->showMessage(QString("Copied %1 name(s) to clipboard").arg(sel.size()));
  };

  auto doCopyPathsClipboard = [&] {
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) return;
    QStringList paths;
    for (const QString & f : sel) paths << (active->path() + (active->path().endsWith('/') ? "" : "/") + f);
    QApplication::clipboard()->setText(paths.join("\n"));
    window.statusBar()->showMessage(QString("Copied %1 path(s) to clipboard").arg(paths.size()));
  };

  auto doInsertToCommandLine = [&] {
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) return;
    QApplication::clipboard()->setText(sel.join(" "));
    window.statusBar()->showMessage(QString("Copied %1 name(s) as command arguments").arg(sel.size()));
  };

  auto doGenerateFileUrl = [&] {
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) return;
    QStringList urls;
    for (const QString & f : sel) {
      QString full = active->path() + (active->path().endsWith('/') ? "" : "/") + f;
      if (active->isRemote() && !connHost.isEmpty())
        urls << QString("sftp://%1@%2:%3%4").arg(connUser, connHost).arg(connPort).arg(full);
      else
        urls << QString("file://%1").arg(full);
    }
    QString text = urls.join("\n");
    QDialog urlDlg(&window);
    urlDlg.setWindowTitle("Generate File URL");
    urlDlg.resize(480, 150);
    auto * lay = new QVBoxLayout(&urlDlg);
    lay->addWidget(new QLabel("File URL(s):"));
    auto * line = new QLineEdit(text, &urlDlg);
    line->selectAll();
    lay->addWidget(line);
    auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &urlDlg);
    bb->button(QDialogButtonBox::Ok)->setText("Copy && Close");
    lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, [&]{
      QApplication::clipboard()->setText(line->text());
      urlDlg.accept();
    });
    QObject::connect(bb, &QDialogButtonBox::rejected, &urlDlg, &QDialog::reject);
    urlDlg.exec();
    window.statusBar()->showMessage(QString("Generated %1 URL(s)").arg(urls.size()));
  };

  auto runCustomCommand = [&](const QString & templateCmd, bool inTerminal = false, bool refreshAfter = true) {
    QStringList sel = active->selectedItems();
    QString selSingle = sel.isEmpty() ? QString() : sel.first();
    QString selJoined;
    for (const QString & s : sel) selJoined += " \"" + s + "\"";
    selJoined = selJoined.trimmed();

    QString cmd = templateCmd;
    QRegularExpression promptRe(R"(!\?([^?]*)\?([^!]*)!)");
    QRegularExpressionMatchIterator it = promptRe.globalMatch(cmd);
    while (it.hasNext()) {
      QRegularExpressionMatch match = it.next();
      QString promptText = match.captured(1);
      QString defVal = match.captured(2);
      bool okIn = false;
      QString userVal = QInputDialog::getText(&window, "Custom Command", promptText, QLineEdit::Normal, defVal, &okIn);
      if (!okIn) return;
      cmd.replace(match.captured(0), userVal);
    }
    cmd.replace("!&", selJoined);
    cmd.replace("!", selSingle);

    if (active->isRemote()) {
      if (!connSsh || connHost.isEmpty()) {
        appMessage(&window, "Custom Command", "No active SSH session.");
        return;
      }
      QString remoteDir = active->path();
      QString jump = (connOpts.tunnel && !connOpts.tunnelHost.empty())
        ? QString("-J %1%2:%3 ").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort)
        : QString();

      if (inTerminal) {
        QString sshCmd = QString("ssh -t %1%2@%3 -p %4 \"cd '%5' && %6; exec bash -l\"")
                         .arg(jump, connUser, connHost).arg(connPort).arg(remoteDir, cmd);
#ifdef Q_OS_MACOS
        QString script = QString("tell application \"Terminal\"\nactivate\ndo script \"%1\"\nend tell").arg(sshCmd);
        QProcess::startDetached("osascript", { "-e", script });
#else
        QProcess::startDetached("x-terminal-emulator", { "-e", sshCmd });
#endif
      } else {
        window.statusBar()->showMessage("Running remote command: " + cmd + "\xE2\x80\xA6");
        QString fullRemoteCmd = QString("cd \"%1\" && %2").arg(remoteDir, cmd);
        QStringList args;
        if (!jump.isEmpty()) {
          args << "-J" << QString("%1%2:%3").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort);
        }
        args << "-p" << QString::number(connPort) << QString("%1@%2").arg(connUser, connHost) << fullRemoteCmd;
        auto * proc = new QProcess(&window);
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [&, proc, cmd, refreshAfter](int exitCode, QProcess::ExitStatus) {
          QString out = QString::fromUtf8(proc->readAllStandardOutput());
          QString err = QString::fromUtf8(proc->readAllStandardError());
          proc->deleteLater();
          if (!out.trimmed().isEmpty() || !err.trimmed().isEmpty()) {
            QDialog resDlg(&window);
            resDlg.setWindowTitle("Command Output: " + cmd);
            resDlg.resize(600, 380);
            auto * lay = new QVBoxLayout(&resDlg);
            auto * te = new QPlainTextEdit(&resDlg);
            te->setReadOnly(true);
            te->setPlainText(out + (err.isEmpty() ? "" : ("\n[Errors]:\n" + err)));
            lay->addWidget(te);
            auto * bb = new QDialogButtonBox(QDialogButtonBox::Close);
            QObject::connect(bb, &QDialogButtonBox::rejected, &resDlg, &QDialog::accept);
            lay->addWidget(bb);
            resDlg.exec();
          }
          if (refreshAfter && active) active->refresh();
          window.statusBar()->showMessage(exitCode == 0 ? "Command completed successfully" : QString("Command exited with code %1").arg(exitCode));
        });
        proc->start("ssh", args);
      }
    } else {
      QString localDir = active->path();
      window.statusBar()->showMessage("Running command: " + cmd + "\xE2\x80\xA6");
      auto * proc = new QProcess(&window);
      proc->setWorkingDirectory(localDir);
      QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [&, proc, cmd, refreshAfter](int exitCode, QProcess::ExitStatus) {
        QString out = QString::fromUtf8(proc->readAllStandardOutput());
        QString err = QString::fromUtf8(proc->readAllStandardError());
        proc->deleteLater();
        if (!out.trimmed().isEmpty() || !err.trimmed().isEmpty()) {
          QDialog resDlg(&window);
          resDlg.setWindowTitle("Command Output: " + cmd);
          resDlg.resize(600, 380);
          auto * lay = new QVBoxLayout(&resDlg);
          auto * te = new QPlainTextEdit(&resDlg);
          te->setReadOnly(true);
          te->setPlainText(out + (err.isEmpty() ? "" : ("\n[Errors]:\n" + err)));
          lay->addWidget(te);
          auto * bb = new QDialogButtonBox(QDialogButtonBox::Close);
          QObject::connect(bb, &QDialogButtonBox::rejected, &resDlg, &QDialog::accept);
          lay->addWidget(bb);
          resDlg.exec();
        }
        if (refreshAfter && active) active->refresh();
        window.statusBar()->showMessage(exitCode == 0 ? "Command completed successfully" : QString("Command exited with code %1").arg(exitCode));
      });
#ifdef Q_OS_WIN
      proc->start("cmd.exe", { "/c", cmd });
#else
      proc->start("/bin/sh", { "-c", cmd });
#endif
    }
  };

  auto doShowInFinder = [&] {
    QStringList sel = active->selectedItems();
    QString p = sel.isEmpty() ? active->path() : (active->path() + "/" + sel.first());
#ifdef Q_OS_MACOS
    QProcess::startDetached("open", QStringList() << "-R" << p);
#else
    QProcess::startDetached("xdg-open", QStringList() << active->path());
#endif
  };

  QAction * actHidden = nullptr;
  auto showPreferences = [&](int defaultTab = 0) {
    QDialog d(&window); d.setWindowTitle("Preferences"); d.resize(520, 360);
    auto * outer = new QVBoxLayout(&d);
    auto * tabs = new QTabWidget; outer->addWidget(tabs, 1);

    // --- Environment ---
    auto * envTab = new QWidget; auto * envLay = new QVBoxLayout(envTab);
    auto * cbHidden = new QCheckBox("Show hidden files"); cbHidden->setChecked(gShowHidden);
    auto * cbDel = new QCheckBox("Confirm file deletion"); cbDel->setChecked(gConfirmDelete);
    auto * cbOvr = new QCheckBox("Confirm overwrites"); cbOvr->setChecked(gConfirmOverwrite);
    auto * cbUpd = new QCheckBox("Check for new &versions at startup"); cbUpd->setChecked(updates::enabled());
    auto * updHint = new QLabel("Asks GitHub for the latest version number, at most once a day. Nothing about you "
                                "or this computer is sent, and nothing is downloaded.");
    updHint->setWordWrap(true); updHint->setStyleSheet("color: gray; margin-left: 22px;");
    envLay->addWidget(cbHidden); envLay->addWidget(cbDel); envLay->addWidget(cbOvr);
    envLay->addSpacing(10); envLay->addWidget(cbUpd); envLay->addWidget(updHint); envLay->addStretch(1);
    tabs->addTab(envTab, "Environment");

    // --- Transfer ---
    auto * trTab = new QWidget; auto * trForm = new QFormLayout(trTab);
    auto * spinPar = new QSpinBox; spinPar->setRange(1, 4); spinPar->setValue(gParallelMax);
    trForm->addRow("Maximum parallel transfers:", spinPar);
    auto * xferSummary = new QLabel(transferSummary(engine::transferSettings())); xferSummary->setWordWrap(true);
    auto * xferEdit = new QPushButton("Transfer settings\xE2\x80\xA6");
    trForm->addRow("Transfer settings:", xferSummary);
    trForm->addRow("", xferEdit);
    QObject::connect(xferEdit, &QPushButton::clicked, &d, [&, xferSummary] {
      if (editTransferSettings(&d)) xferSummary->setText(transferSummary(engine::transferSettings()));
    });
    tabs->addTab(trTab, "Transfer");

    // --- Panels ---
    auto * pnTab = new QWidget; auto * pnLay = new QVBoxLayout(pnTab);
    auto * cbAlt = new QCheckBox("Alternating row colors"); cbAlt->setChecked(gAltColors);
    auto * cmpBox = new QGroupBox("Compare directories");
    auto * cmpLay = new QVBoxLayout(cmpBox);
    auto * cbCmpTime = new QCheckBox("Compare by &time (mark the newer file)"); cbCmpTime->setChecked(gCompareByTime);
    auto * cbCmpSize = new QCheckBox("Compare by &size (mark files whose sizes differ)"); cbCmpSize->setChecked(gCompareBySize);
    auto * cmpHint = new QLabel("Files that exist in one panel only are always marked.");
    cmpHint->setStyleSheet("color: gray;");
    cmpLay->addWidget(cbCmpTime); cmpLay->addWidget(cbCmpSize); cmpLay->addWidget(cmpHint);
    pnLay->addWidget(cbAlt); pnLay->addWidget(cmpBox); pnLay->addStretch(1);
    tabs->addTab(pnTab, "Panels");

    // --- Editors ---
    auto * edTab = new QWidget; auto * edLay = new QVBoxLayout(edTab);
    auto * edGroup = new QGroupBox("Editor for Remote Files");
    auto * edGrpLay = new QVBoxLayout(edGroup);

    auto * rbInternal = new QRadioButton("Built-in text editor (classic WinSCP notepad)");
    auto * rbExternal = new QRadioButton("External editor:");
    if (gUseExternalEditor) rbExternal->setChecked(true);
    else rbInternal->setChecked(true);

    auto * extRow = new QWidget; auto * extLay = new QHBoxLayout(extRow);
    extLay->setContentsMargins(20, 0, 0, 0);
    auto * edPath = new QLineEdit(gExternalEditorPath);
    edPath->setPlaceholderText("e.g. Visual Studio Code, Cursor, /System/Applications/TextEdit.app, or binary path");
    auto * btnBrowse = new QPushButton("Browse\xE2\x80\xA6");
    extLay->addWidget(edPath, 1);
    extLay->addWidget(btnBrowse);

    auto * presetRow = new QWidget; auto * presetLay = new QHBoxLayout(presetRow);
    presetLay->setContentsMargins(20, 0, 0, 0);
    auto * lblQuick = new QLabel("Quick select:");
    auto * comboQuick = new QComboBox;
    comboQuick->addItem("-- Detect / Select installed editor --");
    QStringList commonEditors = {
      "/Applications/Visual Studio Code.app",
      "/Applications/Cursor.app",
      "/Applications/Sublime Text.app",
      "/System/Applications/TextEdit.app",
      "/Applications/kate.app"
    };
    for (const QString & appP : commonEditors) {
      if (QFile::exists(appP)) comboQuick->addItem(QFileInfo(appP).completeBaseName(), appP);
    }
    presetLay->addWidget(lblQuick);
    presetLay->addWidget(comboQuick, 1);

    QObject::connect(comboQuick, QOverload<int>::of(&QComboBox::currentIndexChanged), [&, edPath, comboQuick, rbExternal](int idx) {
      if (idx > 0) {
        edPath->setText(comboQuick->currentData().toString());
        rbExternal->setChecked(true);
      }
    });

    QObject::connect(btnBrowse, &QPushButton::clicked, &d, [&, edPath, rbExternal] {
      QString f = QFileDialog::getOpenFileName(&d, "Select Editor Application", "/Applications",
                                               "Applications & Executables (*.app *);;All Files (*)");
      if (!f.isEmpty()) {
        edPath->setText(f);
        rbExternal->setChecked(true);
      }
    });

    auto * cbDblClick = new QCheckBox("Double-click on remote file opens for editing");
    cbDblClick->setChecked(gDoubleClickToEdit);
    auto * hintDbl = new QLabel("When you save changes in your external editor, PosixSCP automatically re-uploads the file to the remote server.");
    hintDbl->setWordWrap(true);
    hintDbl->setStyleSheet("color: gray; margin-left: 20px;");

    edGrpLay->addWidget(rbInternal);
    edGrpLay->addWidget(rbExternal);
    edGrpLay->addWidget(extRow);
    edGrpLay->addWidget(presetRow);
    edGrpLay->addSpacing(10);
    edGrpLay->addWidget(cbDblClick);
    edGrpLay->addWidget(hintDbl);
    edLay->addWidget(edGroup);
    edLay->addStretch(1);
    tabs->addTab(edTab, "Editors");

    tabs->setCurrentIndex(defaultTab);

    auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (execDialog(d, "preferences") != QDialog::Accepted) return;
    gShowHidden = cbHidden->isChecked(); gConfirmDelete = cbDel->isChecked();
    gConfirmOverwrite = cbOvr->isChecked(); gParallelMax = spinPar->value(); gAltColors = cbAlt->isChecked();
    gCompareByTime = cbCmpTime->isChecked(); gCompareBySize = cbCmpSize->isChecked();
    gUseExternalEditor = rbExternal->isChecked();
    gExternalEditorPath = edPath->text().trimmed();
    gDoubleClickToEdit = cbDblClick->isChecked();
    updates::setEnabled(cbUpd->isChecked());
    if (actHidden) actHidden->setChecked(gShowHidden);
    QSettings s = makeSettings();
    s.setValue("prefs/showHidden", gShowHidden); s.setValue("prefs/confirmDelete", gConfirmDelete);
    s.setValue("prefs/confirmOverwrite", gConfirmOverwrite); s.setValue("prefs/parallelMax", gParallelMax);
    s.setValue("prefs/altColors", gAltColors);
    s.setValue("prefs/compareByTime", gCompareByTime); s.setValue("prefs/compareBySize", gCompareBySize);
    s.setValue("editor/useExternal", gUseExternalEditor);
    s.setValue("editor/externalPath", gExternalEditorPath);
    s.setValue("editor/doubleClickEdit", gDoubleClickToEdit);
    left->view()->setAlternatingRowColors(gAltColors); right->view()->setAlternatingRowColors(gAltColors);
    left->refresh(); right->refresh();
  };

  // Right-click context menu on each panel (WinSCP-style).
  auto installContextMenu = [&](FilePanel * panel) {
    panel->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(panel->view(), &QWidget::customContextMenuRequested, [&, panel](const QPoint & pos) {
      active = panel; panel->onActivated();
      QModelIndex clickedIndex = panel->view()->indexAt(pos);
      if (clickedIndex.isValid()) {
        if (!panel->view()->selectionModel()->isSelected(clickedIndex)) {
          panel->view()->setCurrentIndex(clickedIndex);
          panel->view()->selectRow(clickedIndex.row());
        }
      }
      QMenu menu;
      QStringList selFiles = panel->selectedItems();
      bool hasSel = !selFiles.isEmpty() && clickedIndex.isValid();
      bool isRemote = panel->isRemote();

      if (hasSel) {
        // 1. Open
        auto * actOpen = menu.addAction(QIcon(":/menu_open.png"), "&Open", doOpen);
        actOpen->setShortcut(QKeySequence(Qt::Key_Return));

        // 2. Edit submenu
        auto * editMenu = menu.addMenu(QIcon(":/menu_edit.png"), "&Edit");
        editMenu->addAction(QIcon(":/menu_edit.png"), "&Edit\tF4", [&]{ openWithConfiguredEditor(panel); });
        editMenu->addAction("&Internal editor\tCtrl+Alt+F4", [&]{ openWithEditor(panel, QString(), true); });
        editMenu->addSeparator();

        auto installedEditors = detectInstalledEditors();
        for (const auto & edPair : installedEditors) {
          editMenu->addAction(edPair.first, [&, edPair]{
            openWithEditor(panel, edPair.second, false);
          });
        }
        editMenu->addSeparator();
        editMenu->addAction("Edit &With\xE2\x80\xA6", [&]{
          QString app = QFileDialog::getOpenFileName(&window, "Select Editor Application", "/Applications", "Applications & Executables (*.app *);;All Files (*)");
          if (!app.isEmpty()) openWithEditor(panel, app, false);
        });
        editMenu->addAction(QIcon(":/menu_settings.png"), "&Configure\xE2\x80\xA6", [&]{
          showPreferences(3);
        });

        // 3. Upload / Download submenu
        if (isRemote) {
          auto * dlMenu = menu.addMenu(QIcon(":/menu_download.png"), "&Download\xE2\x80\xA6\tF5");
          dlMenu->addAction(QIcon(":/menu_download.png"), "&Download\xE2\x80\xA6\tF5", doCopy);
          dlMenu->addAction("Download in &Background\xE2\x80\xA6", [&]{
            FilePanel * dst = (active == left) ? right : left;
            if (dst && !dst->isRemote()) startTransfer(active, dst, active->selectedItems(), false, true);
          });
          dlMenu->addSeparator();
          dlMenu->addAction(QIcon(":/menu_move.png"), "Download and &Delete\xE2\x80\xA6\tF6", doMove);
        } else {
          auto * upMenu = menu.addMenu(QIcon(":/menu_upload.png"), "&Upload\xE2\x80\xA6\tF5");
          upMenu->addAction(QIcon(":/menu_upload.png"), "&Upload\xE2\x80\xA6\tF5", doCopy);
          upMenu->addAction("Upload in &Background\xE2\x80\xA6", [&]{
            FilePanel * dst = (active == left) ? right : left;
            if (dst && dst->isRemote()) startTransfer(active, dst, active->selectedItems(), false, true);
          });
          upMenu->addSeparator();
          upMenu->addAction(QIcon(":/menu_move.png"), "Upload and &Delete\xE2\x80\xA6\tF6", doMove);
        }

        // 4. Duplicate
        menu.addAction(QIcon(":/menu_duplicate.png"), "Du&plicate\xE2\x80\xA6\tShift+F5", doDuplicate);

        // 5. Move To...
        menu.addAction(QIcon(":/menu_move.png"), "&Move To\xE2\x80\xA6\tShift+F6", doMoveTo);

        // 6. Delete
        menu.addAction(QIcon(":/menu_delete.png"), "&Delete\tF8", doDelete);

        // 7. Rename
        menu.addAction(QIcon(":/menu_rename.png"), "Rena&me\tF2", doRename);

        menu.addSeparator();

        // 8. Copy (Ctrl+C)
        menu.addAction(QIcon(":/menu_copy.png"), "&Copy\tCtrl+C", doCopyClipboard);

        menu.addSeparator();

        // 9. File Custom Commands
        auto * cmdMenu = menu.addMenu("File &Custom Commands");
        if (isRemote) {
          cmdMenu->addAction("&Execute\tEnter", [&]{ runCustomCommand("\"./!\"", true, true); });
          cmdMenu->addAction("&Touch\tCtrl+T", [&]{ runCustomCommand("touch \"!\"", false, true); });
          cmdMenu->addAction("Ta&r/GZip\xE2\x80\xA6\tShift+Ctrl+R", [&]{ runCustomCommand("tar -cz -f \"!?Archive name?archive.tgz!\" !&", false, true); });
          cmdMenu->addAction("&UnTar/GZip\xE2\x80\xA6\tShift+Ctrl+U", [&]{ runCustomCommand("tar -xz --directory=\"!?Destination directory?.!\" -f \"!\"", false, true); });
          cmdMenu->addAction("&Grep\xE2\x80\xA6\tShift+Ctrl+G", [&]{ runCustomCommand("grep -rn \"!?Pattern?!\" !&", false, false); });
          cmdMenu->addSeparator();
        }
        cmdMenu->addAction("File &Compare", [&]{ doCompareFiles(); });
        cmdMenu->addAction("&Print", [&]{ runCustomCommand("lpr \"!\"", false, false); });
        cmdMenu->addAction("Win&Merge\tCtrl+0", [&]{ doCompareFiles(); });
        cmdMenu->addAction(QIcon(":/menu_terminal.png"), "&Enter\xE2\x80\xA6", [&]{
          bool okIn = false;
          QString c = QInputDialog::getText(&window, "Custom Command", "Enter command to execute (! = current file, !& = all selected files):", QLineEdit::Normal, "", &okIn);
          if (okIn && !c.isEmpty()) runCustomCommand(c, false, true);
        });
        cmdMenu->addSeparator();
        auto * actStatic = cmdMenu->addAction("Static Custom Commands");
        actStatic->setEnabled(false);
        cmdMenu->addAction("Custom Command &Buttons", [&]{ showPreferences(0); });
        cmdMenu->addAction(QIcon(":/menu_settings.png"), "Customi&ze\xE2\x80\xA6", [&]{ showPreferences(0); });

        // 10. File Names
        auto * namesMenu = menu.addMenu("&File Names");
        namesMenu->addAction("&Insert to Command Line\tCtrl+Enter", doInsertToCommandLine);
        namesMenu->addAction("&Copy to Clipboard\tShift+Ctrl+C", doCopyNamesClipboard);
        namesMenu->addAction("Copy to Clipboard (&Include Paths)\tCtrl+Alt+C", doCopyPathsClipboard);
        if (isRemote) {
          namesMenu->addSeparator();
          namesMenu->addAction("&Generate File URL\xE2\x80\xA6", doGenerateFileUrl);
        }

        menu.addSeparator();

        // 11. Properties
        menu.addAction(QIcon(":/menu_properties.png"), "Propert&ies\tF9", doProps);

        // 12. Show in Finder (Local panel only)
        if (!isRemote) {
          menu.addAction("&Show in Finder", doShowInFinder);
        }
      } else {
        // Background context menu (no item clicked or clicked empty space)
        auto * goMenu = menu.addMenu("&Go To");
        goMenu->addAction("&Root", [&]{ panel->navigate("/"); });
        goMenu->addAction("&Home", [&]{ panel->navigate(panel->isRemote() ? remoteHome : u8(engine::homeDir())); });
        goMenu->addAction("&Parent Directory\tBackspace", [&]{ panel->goUp(); });

        auto * newMenu = menu.addMenu("&New");
        newMenu->addAction("&File\xE2\x80\xA6\tShift+F4", doNewFile);
        newMenu->addAction("&Directory\xE2\x80\xA6\tF7", doMkdir);

        menu.addSeparator();
        menu.addAction("Compare &Directories\tShift+F2", doCompareDirs);
        menu.addAction("Calculate Directory &Sizes\tShift+Alt+Enter", doCalcSizes);
        menu.addAction("&Find Files\xE2\x80\xA6\tCtrl+Alt+F", [&]{ doFind(QString()); });
        menu.addSeparator();
        menu.addAction("&Refresh\tCtrl+R", [&]{ panel->refresh(); });
      }

      menu.exec(panel->view()->viewport()->mapToGlobal(pos));
    });
  };
  installContextMenu(left);
  installContextMenu(right);

  // About dialog: icon on top, GPLv3, credit to WinSCP.
  // Shown once on first launch and from Help > Support PosixSCP.
  // PosixSCP sends no usage data. The only request it makes on its own is the optional check for a
  // newer version (updatecheck.h); everything else is traffic to the servers the user connects to.
  auto showWelcome = [&](bool firstRun){
    QDialog d(&window); d.setWindowTitle(firstRun ? "Welcome to PosixSCP" : "Support PosixSCP");
    auto * lay = new QVBoxLayout(&d); lay->setContentsMargins(26, 22, 26, 18); lay->setSpacing(12);
    auto * icon = new QLabel; icon->setAlignment(Qt::AlignHCenter);
    icon->setPixmap(QPixmap(":/posixscp.png").scaled(84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    lay->addWidget(icon);
    auto * body = new QLabel(
      "<div align='center'>"
      "<h2 style='margin:4px'>PosixSCP is 100% free</h2>"
      "<p style='margin:2px;color:gray'>No paid tier, no ads, no account.</p>"
      "</div>"
      "<table cellspacing='8' style='margin-top:6px'>"
      "<tr><td><b>Free&nbsp;forever</b></td><td>Every feature, for everyone. There is nothing to unlock.</td></tr>"
      "<tr><td><b>Open&nbsp;source</b></td><td>GPLv3 &mdash; Free and open source software.</td></tr>"
      "<tr><td><b>No&nbsp;telemetry</b></td><td>No tracking, no usage data, no account. PosixSCP talks to "
      "the servers you connect to; the one exception is an optional check for a newer version (it sends nothing "
      "about you, and Preferences can turn it off).</td></tr>"
      "</table>"
      "<p style='margin:10px 2px 0'>PosixSCP is built for the community, bringing the power and features of WinSCP to "
      "macOS and Linux with native performance and portable configuration.</p>");
    body->setTextFormat(Qt::RichText); body->setOpenExternalLinks(true); body->setWordWrap(true);
    body->setMinimumWidth(430); body->setMaximumWidth(470);
    lay->addWidget(body);
    auto * bb = new QDialogButtonBox;
    auto * go = bb->addButton(firstRun ? "Start using PosixSCP" : "Close", QDialogButtonBox::AcceptRole);
    go->setDefault(true);
    lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    if (!qEnvironmentVariableIsEmpty("WINSCP_WELCOME_SHOT"))   // dev affordance: render the dialog and return
      QTimer::singleShot(150, &d, [&]{ d.grab().save(qEnvironmentVariable("WINSCP_WELCOME_SHOT")); d.accept(); });
    d.exec();
  };

  auto showAbout = [&]{
    QDialog d(&window); d.setWindowTitle("About PosixSCP");
    auto * lay = new QVBoxLayout(&d);
    auto * icon = new QLabel; icon->setAlignment(Qt::AlignHCenter);
    icon->setPixmap(QPixmap(":/posixscp.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    lay->addWidget(icon);
    auto * body = new QLabel(
      "<div align='center'>"
      "<h2 style='margin:4px'>PosixSCP</h2>"
      "<p style='margin:2px;color:gray'>Version " POSIXSCP_VERSION "</p>"
      "<p style='margin:2px'>A native macOS &amp; Linux port of WinSCP.</p>"
      "<p style='margin:2px'>SFTP \xC2\xB7 SCP \xC2\xB7 FTP \xC2\xB7 WebDAV \xC2\xB7 S3 \xC2\xB7 Portable Mode</p>"
      "<p style='margin:8px 2px 2px'><b>100% free &middot; open source &middot; no telemetry.</b></p>"
      "<p style='margin:10px 8px 2px;color:gray'>With gratitude to Martin P\xC5\x99ikryl and the WinSCP "
      "contributors. WinSCP has been a fantastic tool on Windows for many years; PosixSCP brings "
      "its engine and features to POSIX platforms.</p>"
      "</div>");
    body->setTextFormat(Qt::RichText); body->setOpenExternalLinks(true); body->setWordWrap(true);
    body->setMaximumWidth(420);
    lay->addWidget(body);
    auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok); lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    d.exec();
  };

  QAction * actPrefs = nullptr;
  // Menu bar (WinSCP Commander layout: Local, Mark, Files, Commands, Tabs, Options, Remote, Help).
  {
    auto setupBmMenu = [&](QMenu * m) {
      QObject::connect(m, &QMenu::aboutToShow, [&, m]{
        m->clear();
        m->addAction("&Add Current Directory\tCtrl+B", [&]{
          QSettings st = makeSettings(); QStringList bm = st.value("bookmarks").toStringList();
          QString p = active->path();
          if (!bm.contains(p)) { bm << p; st.setValue("bookmarks", bm); st.sync(); }
          window.statusBar()->showMessage("Bookmarked " + p);
        });
        QStringList bm = makeSettings().value("bookmarks").toStringList();
        if (!bm.isEmpty()) {
          m->addSeparator();
          for (const QString & p : bm) {
            auto * a = m->addAction(p);
            QObject::connect(a, &QAction::triggered, [&, p]{ active->navigate(p); });
          }
          m->addSeparator();
          m->addAction("&Remove Current Directory", [&]{
            QSettings st = makeSettings(); QStringList b = st.value("bookmarks").toStringList();
            b.removeAll(active->path()); st.setValue("bookmarks", b); st.sync();
          });
        }
      });
    };

    // 1. &Local
    auto * mLocal = window.menuBar()->addMenu("&Local");
    auto * mLocalNav = mLocal->addMenu("&Navigate");
    mLocalNav->addAction("&Parent Directory\tBackspace", [&]{ left->goUp(); });
    mLocalNav->addAction("&Root\t/", [&]{ left->navigate("/"); });
    mLocalNav->addAction("&Home\tCtrl+H", [&]{ left->navigate(u8(engine::homeDir())); });
    mLocalNav->addAction("&Go to Directory\xE2\x80\xA6\tCtrl+G", [&]{
      bool okIn = false;
      QString p = QInputDialog::getText(&window, "Go to Directory", "Path:", QLineEdit::Normal, left->path(), &okIn);
      if (okIn && !p.isEmpty()) left->navigate(p);
    });
    mLocal->addAction("&Refresh\tCtrl+R", [&]{ left->refresh(); });
    mLocal->addSeparator();
    mLocal->addAction("&Properties\tF9", doProps);
    auto * mLocalBm = mLocal->addMenu("&Bookmarks");
    setupBmMenu(mLocalBm);
    mLocal->addSeparator();
    auto * mLocalSort = mLocal->addMenu("&Sort By");
    mLocalSort->addAction("&Name", [&]{ left->sortByColumn(0); });
    mLocalSort->addAction("&Size", [&]{ left->sortByColumn(1); });
    mLocalSort->addAction("&Changed", [&]{ left->sortByColumn(2); });
    mLocalSort->addAction("&Rights", [&]{ left->sortByColumn(3); });
    mLocalSort->addAction("&Owner", [&]{ left->sortByColumn(4); });
    mLocal->addSeparator();
    mLocal->addAction("&Show in Finder", doShowInFinder);

    // 2. &Mark
    auto * mMark = window.menuBar()->addMenu("&Mark");
    mMark->addAction("&Select Files\xE2\x80\xA6\tCtrl++", [&]{ doSelectMask(true); });
    mMark->addAction("&Unselect Files\xE2\x80\xA6\tCtrl+-", [&]{ doSelectMask(false); });
    mMark->addAction("&Invert Selection\t*", [&]{
      auto * sm = active->view()->selectionModel();
      if (sm && active->view()->model()) {
        for (int r = 0; r < active->view()->model()->rowCount(); ++r)
          sm->select(active->view()->model()->index(r, 0), QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
      }
    });
    mMark->addAction("Select &All\tCtrl+A", [&]{ active->view()->selectAll(); });
    mMark->addAction("&Unselect All", [&]{ active->view()->clearSelection(); });
    mMark->addSeparator();
    mMark->addAction("Calculate Directory Si&zes\tShift+Alt+Enter", doCalcSizes);

    // 3. &Files
    auto * mFiles = window.menuBar()->addMenu("&Files");
    mFiles->addAction("&Open\tF3", doOpen);
    mFiles->addAction("&Edit\tF4", doEdit);
    mFiles->addAction("&Internal Editor\tCtrl+Alt+F4", [&]{ openWithEditor(active, QString(), true); });
    mFiles->addSeparator();
    mFiles->addAction("&Copy / Upload\xE2\x80\xA6\tF5", doCopy);
    mFiles->addAction("&Move\xE2\x80\xA6\tF6", doMove);
    mFiles->addAction("Du&plicate\xE2\x80\xA6\tShift+F5", doDuplicate);
    mFiles->addAction("Rena&me\tF2", doRename);
    mFiles->addAction("&Delete\tF8", doDelete);
    mFiles->addSeparator();
    auto * mFilesNew = mFiles->addMenu("&New");
    mFilesNew->addAction("&File\xE2\x80\xA6\tShift+F4", doNewFile);
    mFilesNew->addAction("&Directory\xE2\x80\xA6\tF7", doMkdir);
    auto * mFilesNames = mFiles->addMenu("File &Names");
    mFilesNames->addAction("&Insert to Command Line\tCtrl+Enter", doInsertToCommandLine);
    mFilesNames->addAction("&Copy to Clipboard\tShift+Ctrl+C", doCopyNamesClipboard);
    mFilesNames->addAction("Copy to Clipboard (&Include Paths)\tCtrl+Alt+C", doCopyPathsClipboard);
    mFilesNames->addAction("&Generate File URL\xE2\x80\xA6", doGenerateFileUrl);
    mFiles->addSeparator();
    mFiles->addAction("&Properties\tF9", doProps);

    // 4. &Commands
    auto * mCommands = window.menuBar()->addMenu("&Commands");
    mCommands->addAction("Open &Terminal\tCtrl+T", doOpenTerminal);
    mCommands->addSeparator();
    mCommands->addAction("&Synchronize\xE2\x80\xA6\tCtrl+S", doSync);
    mCommands->addAction("Synchronize &Browsing\tCtrl+Shift+B", [&]{ actSync->setChecked(!actSync->isChecked()); });
    mCommands->addAction("&Compare Directories\tShift+F2", doCompareDirs);
    mCommands->addAction("Co&mpare Files\tCtrl+Shift+D", [&]{ doCompareFiles(); });
    mCommands->addAction("&Find Files\xE2\x80\xA6\tCtrl+Alt+F", [&]{ doFind(QString()); });
    mCommands->addSeparator();
    mCommands->addAction("Transfer &Queue\tCtrl+Q", [&]{ queueDock->setVisible(!queueDock->isVisible()); });

    // 5. &Tabs
    auto * mTabs = window.menuBar()->addMenu("&Tabs");
    mTabs->addAction("&New Tab\xE2\x80\xA6\tCtrl+N", doConnect);
    mTabs->addAction("&Duplicate Session in Tab", [&]{
      if (engine::remoteConnected()) connectWith(connOpts, (tabBar->count() > 0 && curTab >= 0 && curTab < tabBar->count()) ? tabBar->tabText(curTab) : QString());
    });
    mTabs->addAction("&Close Tab\tCtrl+W", [&]{ if (tabBar->count() > 0) closeTab(tabBar->currentIndex()); });
    mTabs->addSeparator();
    mTabs->addAction("&Next Tab\tCtrl+Tab", [&]{ if (tabBar->count() > 1) tabBar->setCurrentIndex((tabBar->currentIndex() + 1) % tabBar->count()); });
    mTabs->addAction("&Previous Tab\tCtrl+Shift+Tab", [&]{ if (tabBar->count() > 1) tabBar->setCurrentIndex((tabBar->currentIndex() - 1 + tabBar->count()) % tabBar->count()); });
    mTabs->addSeparator();
    mTabs->addAction("&Sites / Login\xE2\x80\xA6", doConnect);
    mTabs->addAction("&Disconnect", doDisconnect);
    mTabs->addSeparator();
    mTabs->addAction("E&xit\tF10", doQuit);

    // 6. &Options
    auto * mOptions = window.menuBar()->addMenu("&Options");
    mOptions->addAction("&Transfer Settings\xE2\x80\xA6", [&]{ editTransferSettings(&window); });
    actHidden = mOptions->addAction("Show &Hidden Files"); actHidden->setCheckable(true); actHidden->setChecked(gShowHidden);
    actHidden->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H));
    QObject::connect(actHidden, &QAction::toggled, [&](bool on){ gShowHidden = on; left->refresh(); right->refresh();
      makeSettings().setValue("prefs/showHidden", on); });
    mOptions->addSeparator();
    mOptions->addAction("Session &Log\tCtrl+L", [&]{ logDock->setVisible(!logDock->isVisible()); });
    mOptions->addAction("Open &Diagnostics Log File", [&]{
      QString p = u8(engine::logPath());
      window.statusBar()->showMessage("Diagnostics log: " + p);
      QProcess::startDetached("open", { "-R", p });
    });
    mOptions->addSeparator();
    actPrefs = mOptions->addAction("&Preferences\xE2\x80\xA6\tCtrl+Alt+P", [&]{ showPreferences(0); });

    // 7. &Remote
    auto * mRemote = window.menuBar()->addMenu("&Remote");
    auto * mRemoteNav = mRemote->addMenu("&Navigate");
    mRemoteNav->addAction("&Parent Directory\tBackspace", [&]{ if (right->isRemote()) right->goUp(); });
    mRemoteNav->addAction("&Root\t/", [&]{ if (right->isRemote()) right->navigate("/"); });
    mRemoteNav->addAction("&Home", [&]{ if (right->isRemote()) right->navigate(remoteHome); });
    mRemoteNav->addAction("&Go to Directory\xE2\x80\xA6\tCtrl+G", [&]{
      if (right->isRemote()) {
        bool okIn = false;
        QString p = QInputDialog::getText(&window, "Go to Remote Directory", "Path:", QLineEdit::Normal, right->path(), &okIn);
        if (okIn && !p.isEmpty()) right->navigate(p);
      }
    });
    mRemote->addAction("&Refresh\tCtrl+R", [&]{ if (right->isRemote()) right->refresh(); });
    mRemote->addSeparator();
    mRemote->addAction("&Properties\tF9", doProps);
    auto * mRemoteBm = mRemote->addMenu("&Bookmarks");
    setupBmMenu(mRemoteBm);
    mRemote->addSeparator();
    auto * mRemoteSort = mRemote->addMenu("&Sort By");
    mRemoteSort->addAction("&Name", [&]{ right->sortByColumn(0); });
    mRemoteSort->addAction("&Size", [&]{ right->sortByColumn(1); });
    mRemoteSort->addAction("&Changed", [&]{ right->sortByColumn(2); });
    mRemoteSort->addAction("&Rights", [&]{ right->sortByColumn(3); });
    mRemoteSort->addAction("&Owner", [&]{ right->sortByColumn(4); });

    // 8. &Help
    auto * mHelp = window.menuBar()->addMenu("&Help");
    mHelp->addAction("Check for &Updates\xE2\x80\xA6", [&]{ updates::check(&window, true); });
    mHelp->addAction("&Support PosixSCP (Free & Open Source)\xE2\x80\xA6", [&]{ showWelcome(false); });
    mHelp->addAction("&About PosixSCP", showAbout);
  }

  //--- function-key bar buttons ------------------------------------------
  auto addFKey = [&](const QString & text, const std::function<void()> & fn) {
    auto * b = new QPushButton(text);
    b->setFocusPolicy(Qt::NoFocus);
    QObject::connect(b, &QPushButton::clicked, fn);
    fkLay->addWidget(b, 1);
    return b;
  };
  addFKey("F2 Rename", doRename);
  addFKey("F4 Edit", doEdit);
  addFKey("F5 Copy", doCopy);
  addFKey("F6 Move", doMove);
  addFKey("F7 Create Directory", doMkdir);
  addFKey("F8 Delete", doDelete);
  addFKey("F9 Properties", doProps);
  addFKey("F10 Quit", doQuit);

  //--- toolbar / shortcuts ------------------------------------------------
  QObject::connect(actCompareDirs, &QAction::triggered, doCompareDirs);
  QObject::connect(actSynchronize, &QAction::triggered, doSync);
  QObject::connect(actConsole, &QAction::triggered, doOpenTerminal);
  QObject::connect(actPutty, &QAction::triggered, doOpenTerminal);
  QObject::connect(actRefresh, &QAction::triggered, [&]{ active->refresh(); });
  QObject::connect(actPrefsBtn, &QAction::triggered, [&]{ showPreferences(0); });
  QObject::connect(tbQueue, &QAction::toggled, [&](bool v){ queueDock->setVisible(v); });
  QObject::connect(queueDock, &QDockWidget::visibilityChanged, [tbQueue](bool v){ tbQueue->setChecked(v); });

  QObject::connect(comboXfer, QOverload<int>::of(&QComboBox::currentIndexChanged), [&, comboXfer](int idx) {
    engine::TransferSettings x = engine::transferSettings();
    if (idx == 0) x.mode = 2; // Automatic
    else if (idx == 1) x.mode = 1; // Text
    else if (idx == 2) x.mode = 0; // Binary
    else if (idx == 3) { editTransferSettings(&window); comboXfer->setCurrentIndex(0); return; }
    applyTransferSettings(x);
  });

  auto * sessionGlobeMenu = new QMenu(&window);
  sessionGlobeMenu->addAction("New Session\xE2\x80\xA6\tCtrl+N", doConnect);
  sessionGlobeMenu->addAction("Reconnect", [&]{
    if (connSsh && !connHost.isEmpty()) {
      QString curLabel = (tabBar->count() > 0 && curTab >= 0 && curTab < tabBar->count()) ? tabBar->tabText(curTab) : QString();
      connectWith(connOpts, curLabel);
    } else {
      doConnect();
    }
  });
  sessionGlobeMenu->addAction("Disconnect", doDisconnect);
  QObject::connect(actGlobe, &QAction::triggered, [&, sessionGlobeMenu]{
    sessionGlobeMenu->exec(QCursor::pos());
  });

  // Tab Bar New Tab dropdown button
  auto * newTabMenu = new QMenu(newTabBtn);
  auto updateNewTabMenu = [&, newTabMenu] {
    newTabMenu->clear();
    newTabMenu->addAction("New Session\xE2\x80\xA6\tCtrl+N", doConnect);
    newTabMenu->addAction("Duplicate Session", [&]{
      if (engine::remoteConnected()) {
        QString curLabel = (tabBar->count() > 0 && curTab >= 0 && curTab < tabBar->count()) ? tabBar->tabText(curTab) : QString();
        connectWith(connOpts, curLabel);
      }
    });
    QStringList sites = savedSiteNames();
    if (!sites.isEmpty()) {
      newTabMenu->addSeparator();
      for (const QString & s : sites) {
        newTabMenu->addAction(QIcon(":/site.png"), s, [&, s]{
          engine::ConnectOptions o;
          if (loadSite(s, o)) connectWith(o, s);
        });
      }
    }
  };
  QObject::connect(newTabMenu, &QMenu::aboutToShow, updateNewTabMenu);
  newTabBtn->setMenu(newTabMenu);
  QObject::connect(newTabBtn, &QToolButton::clicked, doConnect);
  QObject::connect(actDisconnect, &QAction::triggered, doDisconnect);

  auto shortcut = [&](QKeySequence k, const std::function<void()> & fn) {
    auto * a = new QAction(&window); a->setShortcut(k);
    QObject::connect(a, &QAction::triggered, fn); window.addAction(a);
  };
  shortcut(Qt::Key_F2, doRename);
  shortcut(Qt::Key_F3, doOpen);
  shortcut(Qt::Key_F4, doEdit);
  shortcut(Qt::Key_F5, doCopy);
  auto doFileOpenLeft = [&] {
    if (gDoubleClickToEdit) openWithConfiguredEditor(left);
    else doOpen();
  };
  auto doFileOpenRight = [&] {
    if (gDoubleClickToEdit) openWithConfiguredEditor(right);
    else doOpen();
  };
  left->onFileOpen = doFileOpenLeft;
  right->onFileOpen = doFileOpenRight;
  shortcut(Qt::Key_F6, doMove);
  shortcut(Qt::Key_F7, doMkdir);
  shortcut(Qt::Key_F8, doDelete);
  shortcut(QKeySequence::Delete, doDelete);
  shortcut(Qt::Key_F9, doProps);
  shortcut(Qt::Key_F10, doQuit);
  // toggle the session log
  { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_L);
    QObject::connect(a, &QAction::triggered, [&]{ logDock->setVisible(!logDock->isVisible()); });
    window.addAction(a); }
  { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_Q);
    QObject::connect(a, &QAction::triggered, [&]{ queueDock->setVisible(!queueDock->isVisible()); });
    window.addAction(a); }
  // WinSCP keyboard set. Qt::CTRL auto-maps to Command on macOS and Ctrl on Linux/Windows, so these
  // are faithful on Linux and native on mac with no per-OS code. F-keys are identical everywhere.
  shortcut(Qt::CTRL | Qt::Key_R, [&]{ active->refresh(); });               // refresh
  shortcut(QKeySequence::Refresh, [&]{ active->refresh(); });
  shortcut(Qt::ALT | Qt::Key_Left,  [&]{ active->goBack(); });             // history back
  shortcut(Qt::ALT | Qt::Key_Right, [&]{ active->goForward(); });          // history forward
  shortcut(Qt::ALT | Qt::Key_Up,    [&]{ active->goUp(); });               // parent dir
  shortcut(Qt::CTRL | Qt::Key_N, [&]{ doConnect(); });                     // new session
  shortcut(Qt::CTRL | Qt::Key_T, doOpenTerminal);                          // open ssh terminal
  shortcut(Qt::SHIFT | Qt::Key_F4, doNewFile);                             // new empty file
  shortcut(Qt::SHIFT | Qt::Key_F5, doDuplicate);                           // duplicate (within same folder)
  shortcut(Qt::SHIFT | Qt::Key_F6, doMoveTo);                              // move to / rename
  shortcut(Qt::CTRL | Qt::ALT | Qt::Key_F4, [&]{ openWithEditor(active, QString(), true); }); // internal editor
  shortcut(Qt::CTRL | Qt::Key_C, doCopyClipboard);                         // copy to clipboard (MIME + paths)
  shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_C, doCopyNamesClipboard);        // copy file names to clipboard
  shortcut(Qt::CTRL | Qt::ALT | Qt::Key_C, doCopyPathsClipboard);          // copy full file paths to clipboard
  shortcut(Qt::CTRL | Qt::Key_Plus,  [&]{ doSelectMask(true); });          // select by mask
  shortcut(Qt::CTRL | Qt::Key_Equal, [&]{ doSelectMask(true); });          // (same key without shift)
  shortcut(Qt::CTRL | Qt::Key_Minus, [&]{ doSelectMask(false); });         // unselect by mask
  shortcut(Qt::Key_Insert, [&]{ active->toggleSelectAndAdvance(); });      // Norton select + advance
  shortcut(Qt::Key_F1, showAbout);
  shortcut(Qt::CTRL | Qt::ALT | Qt::Key_F, [&]{ doFind(QString()); });        // find files
  shortcut(Qt::SHIFT | Qt::Key_F2, doCompareDirs);                           // compare directories
  shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_D, [&]{ doCompareFiles(); });       // compare files (diff)
  shortcut(Qt::SHIFT | Qt::ALT | Qt::Key_Return, doCalcSizes);               // calculate directory sizes
  shortcut(Qt::SHIFT | Qt::ALT | Qt::Key_Enter, doCalcSizes);

  auto bindPanelCallbacks = [&](FilePanel * p) {
    p->onUpload = doCopy;
    p->onDownload = doCopy;
    p->onMove = doMove;
    p->onEdit = doEdit;
    p->onInternalEdit = [&, p]{ openWithEditor(p, QString(), true); };
    p->onDelete = doDelete;
    p->onProperties = doProps;
    p->onCalculateDirSizes = doCalcSizes;
    p->onNewFile = doNewFile;
    p->onNewDir = doMkdir;
    p->onSelectMask = [&]{ doSelectMask(true); };
    p->onUnselectMask = [&]{ doSelectMask(false); };
    p->onAddBookmark = [&](const QString & path) {
      bool ok = false;
      QString name = QInputDialog::getText(&window, "Add Bookmark", "Bookmark name:", QLineEdit::Normal, path, &ok);
      if (ok && !name.isEmpty()) {
        QSettings s;
        s.setValue("bookmarks/" + name, path);
        window.statusBar()->showMessage("Bookmark added: " + name);
      }
    };
  };
  bindPanelCallbacks(left);
  bindPanelCallbacks(right);

  left->setActive(true);
  left->navigate(u8(engine::homeDir()));
  right->navigate(u8(engine::homeDir()));
  updateStatuses();

  // Persist window geometry + splitter on quit.
  QObject::connect(&app, &QApplication::aboutToQuit, [&]{
    QSettings s;
    s.setValue("win/geometry", window.saveGeometry());
    s.setValue("win/splitter", splitter->saveState());
  });

  window.statusBar()->showMessage(QString::fromUtf8(engine::banner().c_str()));
  window.show();

  // Dev affordances (scripted runs).
  auto optionsFromEnv = [&](engine::ConnectOptions & o) {
    QStringList p = qEnvironmentVariable("WINSCP_AUTOCONNECT").split(':');
    o.host = s8(p.value(0)); o.port = p.value(1).toInt(); o.user = s8(p.value(2)); o.password = s8(p.value(3));
    QString pr = p.value(4);
    o.protocol = pr=="scp"?engine::Protocol::Scp : pr=="dav"?engine::Protocol::WebDav
               : pr=="s3"?engine::Protocol::S3 : pr=="ftp"?engine::Protocol::Ftp : engine::Protocol::Sftp;
    o.keyFile = s8(p.value(5));
    QStringList t = qEnvironmentVariable("WINSCP_AC_TUNNEL").split(':', Qt::SkipEmptyParts);
    if (t.size() >= 3) { o.tunnel = true; o.tunnelHost = s8(t[0]); o.tunnelPort = t[1].toInt(); o.tunnelUser = s8(t[2]);
                         o.tunnelPassword = s8(t.value(3)); o.tunnelKeyFile = s8(t.value(4)); }
    QStringList x = qEnvironmentVariable("WINSCP_AC_PROXY").split(':', Qt::SkipEmptyParts);
    if (x.size() >= 3) { o.proxyMethod = x[0]=="socks4" ? 1 : x[0]=="socks5" ? 2 : 3; o.proxyHost = s8(x[1]); o.proxyPort = x[2].toInt();
                         o.proxyUser = s8(x.value(3)); o.proxyPassword = s8(x.value(4)); }
    if (!qEnvironmentVariableIsEmpty("WINSCP_AC_TLS")) { o.tls = true; o.ftpExplicitTls = qEnvironmentVariable("WINSCP_AC_TLS") != "implicit"; }
    if (qEnvironmentVariable("WINSCP_AC_PASSIVE") == "0") o.ftpPassive = false;
    if (qEnvironmentVariable("WINSCP_AC_CRLF") == "1") o.serverCrLf = true;
    if (!qEnvironmentVariableIsEmpty("WINSCP_AC_UTF8")) o.utf8Names = qEnvironmentVariable("WINSCP_AC_UTF8") == "on" ? 1 : 2;
  };
  auto autoConnectFromEnv = [&] {
      // WINSCP_AUTOCONNECT=host:port:user:pass:proto[:keyfile]; optional WINSCP_AC_TUNNEL=host:port:user:pass[:keyfile]
      // and WINSCP_AC_PROXY=socks4|socks5|http:host:port[:user:pass]. WINSCP_AUTOCONNECT_SITE=<name>
      // connects with a saved site instead (exercises the Login dialog's stored settings).
      QString ac = qEnvironmentVariable("WINSCP_AUTOCONNECT");
      QString acSite = qEnvironmentVariable("WINSCP_AUTOCONNECT_SITE");
      if (!ac.isEmpty() || !acSite.isEmpty()) {
        engine::ConnectOptions o;
        bool have = true;
        if (!acSite.isEmpty()) {
          have = loadSite(acSite, o);
          if (!have) { window.statusBar()->showMessage("Connect failed: no saved site named " + acSite);
                       fprintf(stderr, "[autoconnect] no saved site named %s\n", acSite.toUtf8().constData()); }
        } else optionsFromEnv(o);
        engine::ConnectResult r;
        if (have) r = engine::connect(o);
        if (have && r.ok) { sessionOpened(o, r);
          if (!qEnvironmentVariable("WINSCP_DEMO").isEmpty()) {   // marketing screenshot: generic dirs
            left->navigate("/tmp/freescp-demo");
            right->navigate("/config/demo");
          }
          // exercise the BACKGROUND transfer queue end-to-end (multi-file, on the worker thread)
          QString xf = qEnvironmentVariable("WINSCP_AUTOXFER");
          if (!xf.isEmpty()) {
            QDir().mkpath("/tmp/dlq"); left->navigate("/tmp/dlq");
            startTransfer(right, left, xf.split(','), false);
          }
        }
        else if (have)   // make a failed auto-connect visible in the render (verifies the error text the user would see)
        { window.statusBar()->showMessage("Connect failed: " + QString::fromUtf8(r.error.c_str()));
          fprintf(stderr, "[autoconnect] failed: %s\n", r.error.c_str()); }
      }
  };

  // WINSCP_UITEST="cmd;cmd;..." drives the UI without a human, after the optional auto-connect.
  // Commands are registered in uiCmd by the features they exercise; the generic ones are below.
  // Arguments are separated by '|'. Each command runs to completion before the next starts.
  uiCmd["wait"] = [&](const QStringList & a) { QEventLoop l; QTimer::singleShot(a.value(0).toInt(), &l, &QEventLoop::quit); l.exec(); };
  uiCmd["waitidle"] = [&](const QStringList &) {   // until the background transfer batch has finished
    QEventLoop l; QTimer tm; QObject::connect(&tm, &QTimer::timeout, [&]{ if (!gTransferRunning.load()) l.quit(); });
    tm.start(50); l.exec();
    QEventLoop l2; QTimer::singleShot(150, &l2, &QEventLoop::quit); l2.exec();   // let the queued completion handlers run
  };
  uiCmd["shot"] = [&](const QStringList & a) { window.grab().save(a.value(0)); };
  uiCmd["login"] = [&](const QStringList &) { doConnect(); };
  // connect|host:port:user:pass:proto : open ANOTHER session (a new tab);  tab|<index> : switch;  closetab|<index>
  uiCmd["connect"] = [&](const QStringList & a) {
    QStringList p = a.value(0).split(':');
    engine::ConnectOptions o;
    o.host = s8(p.value(0)); o.port = p.value(1).toInt(); o.user = s8(p.value(2)); o.password = s8(p.value(3));
    QString pr = p.value(4);
    o.protocol = pr=="scp"?engine::Protocol::Scp : pr=="dav"?engine::Protocol::WebDav
               : pr=="s3"?engine::Protocol::S3 : pr=="ftp"?engine::Protocol::Ftp : engine::Protocol::Sftp;
    connectWith(o);
  };
  uiCmd["tab"] = [&](const QStringList & a) { tabBar->setCurrentIndex(a.value(0).toInt()); };
  uiCmd["closetab"] = [&](const QStringList & a) { closeTab(a.value(0).toInt()); };
  uiCmd["tabs"] = [&](const QStringList &) {
    fprintf(stdout, "[tabs] count=%d current=%d engine-active=%d\n", tabBar->count(), tabBar->currentIndex(), engine::activeSession()); fflush(stdout);
  };
  uiCmd["find"] = [&](const QStringList & a) {          // find|left|*.txt
    (a.value(0) == "right" ? right : left)->onActivated(); doFind(a.value(1, "*"));
  };
  uiCmd["dirsizes"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->onActivated(); doCalcSizes(); };
  uiCmd["comparedirs"] = [&](const QStringList & a) {   // comparedirs[|time|size]
    if (a.size() >= 1 && !a.value(0).isEmpty()) { gCompareByTime = a.contains("time"); gCompareBySize = a.contains("size"); }
    doCompareDirs();
  };
  uiCmd["comparefiles"] = [&](const QStringList & a) {  // comparefiles|left  (uses that panel's selection)
    (a.value(0) == "right" ? right : left)->onActivated();
    QString r = doCompareFiles();
    fprintf(stdout, "[compare] %s\n", r.toUtf8().constData()); fflush(stdout);
  };
  uiCmd["checkupdate"] = [&](const QStringList &) { updates::check(&window, true); };   // Help > Check for Updates
  uiCmd["prefs"] = [&](const QStringList &) { if (actPrefs) actPrefs->trigger(); };
  uiCmd["xfersettings"] = [&](const QStringList &) { editTransferSettings(&window); };   // the dialog (render / accept)
  uiCmd["printxfer"] = [&](const QStringList &) {
    fprintf(stdout, "[xfer] %s\n", transferSummary(engine::transferSettings()).toUtf8().constData()); fflush(stdout);
  };
  uiCmd["setxfer"] = [&](const QStringList & a) {   // setxfer|mode=text|speed=300|... (persisted, like the dialog)
    engine::TransferSettings x = engine::transferSettings();
    for (const QString & kv : a) {
      const QString k = kv.section('=', 0, 0), v = kv.section('=', 1);
      if (k == "mode") x.mode = v == "text" ? 1 : v == "auto" ? 2 : 0;
      else if (k == "textmask") x.textMask = s8(v);
      else if (k == "time") x.preserveTime = v != "0";
      else if (k == "rights") { x.setRights = !v.isEmpty(); if (!v.isEmpty()) x.rightsOctal = s8(v); }
      else if (k == "case") x.fileNameCase = v == "upper" ? 1 : v == "lower" ? 2 : 0;
      else if (k == "resume") x.resume = v == "on" ? 0 : v == "off" ? 2 : 1;
      else if (k == "threshold") x.resumeThresholdKB = v.toLongLong();
      else if (k == "speed") x.speedLimitKB = v.toInt();
      else if (k == "mask") x.fileMask = s8(QString(v).replace('^', '|'));
      else if (k == "newer") x.newerOnly = v != "0";
      else if (k == "hidden") x.excludeHidden = v == "exclude";
      else if (k == "defaults") x = engine::TransferSettings();
    }
    QString err;
    if (!validateTransferSettings(x, &err)) { fprintf(stderr, "[uitest] invalid transfer settings: %s\n", err.toUtf8().constData()); return; }
    applyTransferSettings(x);
  };
  // xfer|right|left : transfer the source panel's selection to the other panel (no dialog)
  uiCmd["xfer"] = [&](const QStringList & a) {
    FilePanel * from = (a.value(0) == "right" ? right : left), * to = (from == right ? left : right);
    startTransfer(from, to, from->selectedItems(), false);
  };
  uiCmd["copy"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->onActivated(); doCopy(); };   // F5 with its dialog
  // The Login dialog pre-filled from the WINSCP_AUTOCONNECT / WINSCP_AC_* variables; connects if accepted.
  uiCmd["loginpreset"] = [&](const QStringList &) {
    engine::ConnectOptions o; optionsFromEnv(o);
    LoginParams lp = showLoginDialog(&window, &o);
    if (lp.ok) connectWith(lp.o, lp.siteName);
  };
  uiCmd["nav"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->navigate(a.value(1)); };
  uiCmd["activate"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->onActivated(); };
  uiCmd["select"] = [&](const QStringList & a) {   // select|left|*.txt  (replaces the selection)
    FilePanel * pn = (a.value(0) == "right" ? right : left);
    pn->view()->clearSelection(); pn->selectByMask(a.value(1, "*"), true);
  };
  uiCmd["print"] = [&](const QStringList & a) {    // print|left : the panel's listing, for assertions
    FilePanel * pn = (a.value(0) == "right" ? right : left);
    fprintf(stdout, "[panel %s] %s\n", a.value(0).toUtf8().constData(), pn->path().toUtf8().constData());
    for (const QString & line : pn->dump()) fprintf(stdout, "  %s\n", line.toUtf8().constData());
    fflush(stdout);
  };
  const QStringList uiTest = qEnvironmentVariable("WINSCP_UITEST").split(';', Qt::SkipEmptyParts);
  if (!uiTest.isEmpty())
    QMetaObject::invokeMethod(&window, [&, uiTest]{
      autoConnectFromEnv();
      for (const QString & step : uiTest)
      {
        QStringList parts = step.split('|');
        const QString name = parts.takeFirst().trimmed();
        if (name == "quit") break;
        auto it = uiCmd.find(name);
        if (it == uiCmd.end()) { fprintf(stderr, "[uitest] unknown command: %s\n", name.toUtf8().constData()); continue; }
        fprintf(stderr, "[uitest] %s\n", step.toUtf8().constData());
        it->second(parts);
        QApplication::processEvents();
      }
      app.quit();
    }, Qt::QueuedConnection);
  // WINSCP_SHOT=path renders the main window to a PNG and quits (UI verification).
  else if (!qEnvironmentVariableIsEmpty("WINSCP_SHOT"))
    QMetaObject::invokeMethod(&window, [&]{
      autoConnectFromEnv();
      // Grab after a delay so background transfers (detached worker) complete first.
      QTimer::singleShot(qEnvironmentVariableIsEmpty("WINSCP_AUTOXFER") ? 100 : 3000, [&]{
        window.grab().save(qEnvironmentVariable("WINSCP_SHOT"));
        app.quit();
      });
    }, Qt::QueuedConnection);

  // First launch: say once that the app is free / open source / telemetry-free and how to support it.
  // Never in headless or scripted runs (offscreen platform, render hooks, smoke tests).
  const bool scripted = uiScripted() || !qEnvironmentVariableIsEmpty("WINSCP_NO_AUTOLOGIN");
  const bool welcomeDue = !qEnvironmentVariableIsEmpty("WINSCP_WELCOME_SHOT")
                          || (!scripted && !QSettings().value("ui/welcomeShown", false).toBool());

  // Offer the Login dialog on startup (like WinSCP), unless launched headless for a smoke test.
  QMetaObject::invokeMethod(&window, [&, welcomeDue]{
    if (welcomeDue)
    {
      showWelcome(true);
      if (!qEnvironmentVariableIsEmpty("WINSCP_WELCOME_SHOT")) { app.quit(); return; }
      QSettings().setValue("ui/welcomeShown", true);
    }
    if (qEnvironmentVariableIsEmpty("WINSCP_NO_AUTOLOGIN") && qEnvironmentVariableIsEmpty("WINSCP_UITEST")) doConnect();
  }, Qt::QueuedConnection);

  // A newer version? (at most once a day, optional - see updatecheck.h). After the first-launch note.
  if (!welcomeDue) QTimer::singleShot(1200, &window, [&]{ updates::check(&window, false); });

  int rc = app.exec();
  // Close sessions while the engine is alive (see engine::shutdown). If a transfer is still running
  // on the worker thread, skip all destructors rather than pull the engine from under it.
  // A background job (transfer, size calculation, search) still running holds references to the
  // window: leave without running destructors rather than let it touch freed objects.
  if (gTransferRunning.load()) { fflush(nullptr); std::_Exit(rc); }
  if (!engine::shutdown(3000)) { fflush(nullptr); std::_Exit(rc); }
  return rc;
}
