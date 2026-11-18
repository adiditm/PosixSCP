//---------------------------------------------------------------------------
// logindialog.cpp — see logindialog.h.
//
// Login dialog, faithful to WinSCP: a sites tree on the left, a "Session" form on the right (File
// protocol, Encryption, Host name + Port, User name, Password, Private key) and an "Advanced..."
// dialog for what WinSCP keeps under Advanced Site Settings: Connection (FTP passive mode),
// Proxy and SSH Tunnel.
//---------------------------------------------------------------------------
#include "logindialog.h"
#include "uicommon.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QSplitter>
#include <QTextEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QTextStream>
#include <QFile>
#include <QMap>
#include <QCoreApplication>

namespace {

// Protocol <-> combo index (the index is also what saved sites store as "protocol").
const engine::Protocol kProtocols[] = { engine::Protocol::Sftp, engine::Protocol::Scp, engine::Protocol::Ftp,
                                        engine::Protocol::WebDav, engine::Protocol::S3 };
int protocolIndex(engine::Protocol p)
{
  for (int i = 0; i < 5; ++i) if (kProtocols[i] == p) return i;
  return 0;
}
bool isSsh(engine::Protocol p) { return p == engine::Protocol::Sftp || p == engine::Protocol::Scp; }

int defaultPort(engine::Protocol p, bool tls, bool ftpExplicit)
{
  switch (p)
  {
    case engine::Protocol::Ftp:    return (tls && !ftpExplicit) ? 990 : 21;
    case engine::Protocol::WebDav: return tls ? 443 : 80;
    case engine::Protocol::S3:     return tls ? 443 : 80;
    default:                       return 22;
  }
}
int defaultProxyPort(int method) { return (method == 3) ? 80 : 1080; }

// A small "file path + browse" row.
QWidget * keyRow(QWidget * dlg, QLineEdit * edit)
{
  auto * row = new QWidget;
  auto * lay = new QHBoxLayout(row);
  lay->setContentsMargins(0, 0, 0, 0);
  auto * btn = new QToolButton; btn->setText("\xE2\x80\xA6");
  lay->addWidget(edit, 1); lay->addWidget(btn);
  QObject::connect(btn, &QToolButton::clicked, dlg, [dlg, edit] {
    QString f = QFileDialog::getOpenFileName(dlg, "Select SSH private key", QDir::homePath() + "/.ssh");
    if (!f.isEmpty()) edit->setText(f);
  });
  return row;
}

} // namespace

// Advanced site settings. Edits the advanced members of `o` in place; returns true on OK.
bool showAdvancedDialog(QWidget * parent, engine::ConnectOptions & o)
{
  QDialog d(parent);
  d.setWindowTitle("Advanced Site Settings");
  d.resize(650, 480);
  d.setSizeGripEnabled(true);

  auto * outer = new QVBoxLayout(&d);
  outer->setContentsMargins(10, 10, 10, 10);
  outer->setSpacing(10);

  auto * hBox = new QHBoxLayout;
  outer->addLayout(hBox, 1);

  // Left category tree
  auto * catTree = new QTreeWidget(&d);
  catTree->setHeaderHidden(true);
  catTree->setFixedWidth(165);
  catTree->setIndentation(16);
  catTree->setRootIsDecorated(true);
  catTree->setAnimated(true);
  hBox->addWidget(catTree);

  // Right stacked widget
  auto * pages = new QStackedWidget(&d);
  hBox->addWidget(pages, 1);

  // Helper to build tree items and map to stack index
  auto addCat = [&](QTreeWidgetItem * parentItem, const QString & text, int pageIdx, bool selected = false) -> QTreeWidgetItem* {
    auto * it = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(catTree);
    it->setText(0, text);
    it->setData(0, Qt::UserRole, pageIdx);
    if (selected) catTree->setCurrentItem(it);
    return it;
  };

  auto * itemEnv = addCat(nullptr, "Environment", -1);
  auto * itemDir = addCat(itemEnv, "Directories", 0, true);
  auto * itemRec = addCat(itemEnv, "Recycle bin", 1);
  auto * itemSh  = addCat(itemEnv, "SCP/Shell", 2);
  itemEnv->setExpanded(true);

  auto * itemConn = addCat(nullptr, "Connection", 3);
  auto * itemProxy = addCat(itemConn, "Proxy", 4);
  auto * itemTunnel = addCat(itemConn, "Tunnel", 5);
  itemConn->setExpanded(true);

  auto * itemSsh = addCat(nullptr, "SSH", 6);
  auto * itemKex = addCat(itemSsh, "Key exchange", 7);
  auto * itemAuth = addCat(itemSsh, "Authentication", 8);
  auto * itemBugs = addCat(itemSsh, "Bugs", 9);
  itemSsh->setExpanded(true);

  auto * itemNote = addCat(nullptr, "Note", 10);

  QObject::connect(catTree, &QTreeWidget::currentItemChanged, &d, [&](QTreeWidgetItem * cur, QTreeWidgetItem *) {
    if (!cur) return;
    int idx = cur->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < pages->count()) {
      pages->setCurrentIndex(idx);
    } else if (cur->childCount() > 0) {
      catTree->setCurrentItem(cur->child(0));
    }
  });

  //===========================================================================
  // Page 0: Environment -> Directories (MATCHING WINSCP SCREENSHOT)
  //===========================================================================
  auto * pageDir = new QWidget(&d);
  auto * layDir = new QVBoxLayout(pageDir);
  layDir->setContentsMargins(0, 0, 0, 0);
  layDir->setSpacing(10);

  // Group: Directories
  auto * grpDir = new QGroupBox("Directories", pageDir);
  auto * vDir = new QVBoxLayout(grpDir);
  vDir->setSpacing(8);

  auto * cbSyncBrowsing = new QCheckBox("&Synchronize browsing", grpDir);
  cbSyncBrowsing->setChecked(o.syncBrowsing);
  auto * cbRememberLastDir = new QCheckBox("Re&member last used directory", grpDir);
  cbRememberLastDir->setChecked(o.rememberLastDir);

  auto * lblRemote = new QLabel("&Remote directory:", grpDir);
  auto * editRemoteDir = new QLineEdit(u8(o.remoteDir), grpDir);
  editRemoteDir->setPlaceholderText("e.g. /home/user or /public_html");
  lblRemote->setBuddy(editRemoteDir);

  auto * lblLocal = new QLabel("&Local directory:", grpDir);
  auto * rowLocal = new QHBoxLayout;
  auto * editLocalDir = new QLineEdit(u8(o.localDir), grpDir);
  editLocalDir->setPlaceholderText("e.g. /Users/name/Projects or Z:\\ProjectsY\\...");
  lblLocal->setBuddy(editLocalDir);
  auto * btnBrowseLocal = new QToolButton(grpDir);
  btnBrowseLocal->setText("...");
  btnBrowseLocal->setToolTip("Browse local directory");
  QObject::connect(btnBrowseLocal, &QToolButton::clicked, &d, [&] {
    QString start = resolveLocalPath(editLocalDir->text());
    if (start.isEmpty() || !QDir(start).exists()) start = QDir::homePath();
    QString chosen = QFileDialog::getExistingDirectory(&d, "Select Local Directory", start);
    if (!chosen.isEmpty()) editLocalDir->setText(chosen);
  });
  rowLocal->addWidget(editLocalDir, 1);
  rowLocal->addWidget(btnBrowseLocal);

  auto * lblDirNotice = new QLabel("Local directory is not used with Explorer interface.", grpDir);
  lblDirNotice->setStyleSheet("color: #777; font-size: 11px;");

  vDir->addWidget(cbSyncBrowsing);
  vDir->addWidget(cbRememberLastDir);
  vDir->addSpacing(4);
  vDir->addWidget(lblRemote);
  vDir->addWidget(editRemoteDir);
  vDir->addWidget(lblLocal);
  vDir->addLayout(rowLocal);
  vDir->addWidget(lblDirNotice);
  layDir->addWidget(grpDir);

  // Group: Directory reading options
  auto * grpRead = new QGroupBox("Directory reading options", pageDir);
  auto * gridRead = new QGridLayout(grpRead);
  gridRead->setSpacing(8);

  auto * cbCacheVisited = new QCheckBox("Cache &visited remote directories", grpRead);
  cbCacheVisited->setChecked(o.cacheVisitedDirs);
  auto * cbCacheChanges = new QCheckBox("Cache &directory changes", grpRead);
  cbCacheChanges->setChecked(o.cacheDirChanges);
  auto * cbPermCache = new QCheckBox("&Permanent cache", grpRead);
  cbPermCache->setChecked(o.permanentCache);
  auto * cbResolveSymlinks = new QCheckBox("&Resolve symbolic links", grpRead);
  cbResolveSymlinks->setChecked(o.resolveSymlinks);
  auto * cbFollowSymlinks = new QCheckBox("&Follow symbolic links to directories", grpRead);
  cbFollowSymlinks->setChecked(o.followSymlinkDirs);

  gridRead->addWidget(cbCacheVisited, 0, 0);
  gridRead->addWidget(cbCacheChanges, 1, 0);
  gridRead->addWidget(cbPermCache, 1, 1);
  gridRead->addWidget(cbResolveSymlinks, 2, 0);
  gridRead->addWidget(cbFollowSymlinks, 3, 0);
  gridRead->setColumnStretch(0, 1);
  gridRead->setColumnStretch(1, 1);
  layDir->addWidget(grpRead);
  layDir->addStretch(1);
  pages->addWidget(pageDir); // index 0

  //===========================================================================
  // Page 1: Environment -> Recycle bin
  //===========================================================================
  auto * pageRec = new QWidget(&d);
  auto * layRec = new QVBoxLayout(pageRec);
  auto * grpRec = new QGroupBox("Recycle bin", pageRec);
  auto * vRec = new QVBoxLayout(grpRec);
  auto * cbUseRecycle = new QCheckBox("Delete files to &recycle bin", grpRec);
  auto * editRecyclePath = new QLineEdit(".recycle", grpRec);
  vRec->addWidget(cbUseRecycle);
  vRec->addWidget(new QLabel("Recycle bin directory on remote host:"));
  vRec->addWidget(editRecyclePath);
  vRec->addStretch(1);
  layRec->addWidget(grpRec);
  layRec->addStretch(1);
  pages->addWidget(pageRec); // index 1

  //===========================================================================
  // Page 2: Environment -> SCP/Shell
  //===========================================================================
  auto * pageSh = new QWidget(&d);
  auto * laySh = new QVBoxLayout(pageSh);
  auto * grpSh = new QGroupBox("SCP / Shell", pageSh);
  auto * vSh = new QVBoxLayout(grpSh);
  auto * cbCrLf = new QCheckBox("Server uses CR/&LF line endings (Windows server)", grpSh);
  cbCrLf->setChecked(o.serverCrLf);
  cbCrLf->setEnabled(isSsh(o.protocol));
  auto * crlfHint = new QLabel("Used by text-mode transfers over SFTP and SCP to convert line endings.", grpSh);
  crlfHint->setWordWrap(true); crlfHint->setStyleSheet("color: gray;");
  auto * utfRow = new QHBoxLayout;
  auto * utfCombo = new QComboBox(grpSh); utfCombo->addItems({ "Auto", "On", "Off" });
  utfCombo->setCurrentIndex(qBound(0, o.utf8Names, 2));
  utfRow->addWidget(new QLabel("&UTF-8 encoding for filenames:")); utfRow->addWidget(utfCombo); utfRow->addStretch(1);
  auto * utfHint = new QLabel("Choose On if file names with accents or non-Latin characters look wrong.", grpSh);
  utfHint->setWordWrap(true); utfHint->setStyleSheet("color: gray;");
  vSh->addWidget(cbCrLf); vSh->addWidget(crlfHint);
  vSh->addSpacing(8);
  vSh->addLayout(utfRow); vSh->addWidget(utfHint);
  vSh->addStretch(1);
  laySh->addWidget(grpSh);
  laySh->addStretch(1);
  pages->addWidget(pageSh); // index 2

  //===========================================================================
  // Page 3: Connection (MATCHING WINSCP SCREENSHOT)
  //===========================================================================
  auto * pageConn = new QWidget(&d);
  auto * layConn = new QVBoxLayout(pageConn);
  layConn->setContentsMargins(0, 0, 0, 0);
  layConn->setSpacing(8);

  // Group 1: Connection
  auto * grpConn = new QGroupBox("Connection", pageConn);
  auto * vConn = new QVBoxLayout(grpConn);
  vConn->setSpacing(6);
  auto * cbPassive = new QCheckBox("&Passive mode", grpConn);
  cbPassive->setChecked(o.ftpPassive);
  cbPassive->setEnabled(o.protocol == engine::Protocol::Ftp);
  auto * cbOptBuf = new QCheckBox("&Optimize connection buffer size", grpConn);
  cbOptBuf->setChecked(o.optimizeSendBuf);
  vConn->addWidget(cbPassive);
  vConn->addWidget(cbOptBuf);
  layConn->addWidget(grpConn);

  // Group 2: Timeouts
  auto * grpTimeout = new QGroupBox("Timeouts", pageConn);
  auto * layTimeout = new QHBoxLayout(grpTimeout);
  auto * lblTimeout = new QLabel("Server &response timeout:", grpTimeout);
  auto * spinTimeout = new QSpinBox(grpTimeout);
  spinTimeout->setRange(1, 3600);
  spinTimeout->setValue(o.timeout > 0 ? o.timeout : 15);
  lblTimeout->setBuddy(spinTimeout);
  auto * lblSecs = new QLabel("seconds", grpTimeout);
  layTimeout->addWidget(lblTimeout);
  layTimeout->addStretch(1);
  layTimeout->addWidget(spinTimeout);
  layTimeout->addWidget(lblSecs);
  layConn->addWidget(grpTimeout);

  // Group 3: Keepalives
  auto * grpKeep = new QGroupBox("Keepalives", pageConn);
  auto * vKeep = new QVBoxLayout(grpKeep);
  vKeep->setSpacing(6);
  auto * rbKeepOff = new QRadioButton("&Off", grpKeep);
  auto * rbKeepNull = new QRadioButton("Sending of &null SSH packets", grpKeep);
  auto * rbKeepDummy = new QRadioButton("Executing &dummy protocol commands", grpKeep);
  auto * bgKeep = new QButtonGroup(&d);
  bgKeep->addButton(rbKeepOff, 0);
  bgKeep->addButton(rbKeepNull, 1);
  bgKeep->addButton(rbKeepDummy, 2);
  if (o.pingType == 1) rbKeepNull->setChecked(true);
  else if (o.pingType == 2) rbKeepDummy->setChecked(true);
  else rbKeepOff->setChecked(true);

  auto * rowPing = new QHBoxLayout;
  auto * lblPing = new QLabel("Seconds &between keepalives:", grpKeep);
  auto * spinPing = new QSpinBox(grpKeep);
  spinPing->setRange(1, 3600);
  spinPing->setValue(o.pingInterval > 0 ? o.pingInterval : 30);
  lblPing->setBuddy(spinPing);
  rowPing->addWidget(lblPing);
  rowPing->addStretch(1);
  rowPing->addWidget(spinPing);

  auto syncKeep = [=] {
    bool on = !rbKeepOff->isChecked();
    lblPing->setEnabled(on);
    spinPing->setEnabled(on);
  };
  QObject::connect(rbKeepOff, &QRadioButton::toggled, &d, syncKeep);
  QObject::connect(rbKeepNull, &QRadioButton::toggled, &d, syncKeep);
  QObject::connect(rbKeepDummy, &QRadioButton::toggled, &d, syncKeep);
  syncKeep();

  vKeep->addWidget(rbKeepOff);
  vKeep->addWidget(rbKeepNull);
  vKeep->addWidget(rbKeepDummy);
  vKeep->addLayout(rowPing);
  layConn->addWidget(grpKeep);

  // Group 4: Internet protocol version
  auto * grpIp = new QGroupBox("Internet protocol version", pageConn);
  auto * rowIp = new QHBoxLayout(grpIp);
  auto * rbIpAuto = new QRadioButton("&Auto", grpIp);
  auto * rbIp4 = new QRadioButton("IPv&4", grpIp);
  auto * rbIp6 = new QRadioButton("IPv&6", grpIp);
  auto * bgIp = new QButtonGroup(&d);
  bgIp->addButton(rbIpAuto, 0);
  bgIp->addButton(rbIp4, 1);
  bgIp->addButton(rbIp6, 2);
  if (o.addressFamily == 1) rbIp4->setChecked(true);
  else if (o.addressFamily == 2) rbIp6->setChecked(true);
  else rbIpAuto->setChecked(true);

  rowIp->addWidget(rbIpAuto);
  rowIp->addSpacing(30);
  rowIp->addWidget(rbIp4);
  rowIp->addSpacing(30);
  rowIp->addWidget(rbIp6);
  rowIp->addStretch(1);
  layConn->addWidget(grpIp);

  layConn->addStretch(1);
  pages->addWidget(pageConn); // index 3

  //===========================================================================
  // Page 4: Connection -> Proxy
  //===========================================================================
  auto * pageProxy = new QWidget(&d);
  auto * layProxy = new QVBoxLayout(pageProxy);
  auto * grpProxy = new QGroupBox("Proxy", pageProxy);
  auto * pxForm = new QFormLayout(grpProxy);
  auto * pxType = new QComboBox(grpProxy); pxType->addItems({ "None", "SOCKS4", "SOCKS5", "HTTP" });
  pxType->setCurrentIndex(qBound(0, o.proxyMethod, 3));
  auto * pxHost = new QLineEdit(u8(o.proxyHost), grpProxy); pxHost->setPlaceholderText("proxy host name");
  auto * pxPort = new QSpinBox(grpProxy); pxPort->setRange(1, 65535);
  pxPort->setValue(o.proxyPort > 0 ? o.proxyPort : defaultProxyPort(o.proxyMethod));
  auto * pxUser = new QLineEdit(u8(o.proxyUser), grpProxy);
  auto * pxPass = new QLineEdit(u8(o.proxyPassword), grpProxy); pxPass->setEchoMode(QLineEdit::Password);
  pxForm->addRow("Proxy &type:", pxType);
  pxForm->addRow("Proxy &host name:", pxHost);
  pxForm->addRow("Po&rt number:", pxPort);
  pxForm->addRow("&User name:", pxUser);
  pxForm->addRow("Pass&word:", pxPass);
  auto * pxHint = new QLabel("SFTP and SCP connections to localhost are never sent through the proxy.", grpProxy);
  pxHint->setWordWrap(true); pxHint->setStyleSheet("color: gray;");
  pxForm->addRow(pxHint);
  auto syncProxy = [=] {
    bool on = pxType->currentIndex() != 0;
    for (QWidget * w : { (QWidget *)pxHost, (QWidget *)pxPort, (QWidget *)pxUser, (QWidget *)pxPass }) w->setEnabled(on);
  };
  QObject::connect(pxType, &QComboBox::currentIndexChanged, &d, [=](int i) {
    if (pxPort->value() == 1080 || pxPort->value() == 80) pxPort->setValue(defaultProxyPort(i));
    syncProxy();
  });
  syncProxy();
  layProxy->addWidget(grpProxy);
  layProxy->addStretch(1);
  pages->addWidget(pageProxy); // index 4

  //===========================================================================
  // Page 5: Connection -> Tunnel
  //===========================================================================
  auto * pageTunnel = new QWidget(&d);
  auto * layTunnel = new QVBoxLayout(pageTunnel);
  auto * grpTunnel = new QGroupBox("SSH tunnel", pageTunnel);
  auto * tnForm = new QFormLayout(grpTunnel);
  auto * tnOn = new QCheckBox("&Connect through SSH tunnel", grpTunnel);
  tnOn->setChecked(o.tunnel);
  auto * tnHost = new QLineEdit(u8(o.tunnelHost), grpTunnel); tnHost->setPlaceholderText("SSH server to tunnel through (jump host)");
  auto * tnPort = new QSpinBox(grpTunnel); tnPort->setRange(1, 65535); tnPort->setValue(o.tunnelPort > 0 ? o.tunnelPort : 22);
  auto * tnUser = new QLineEdit(u8(o.tunnelUser), grpTunnel);
  auto * tnPass = new QLineEdit(u8(o.tunnelPassword), grpTunnel); tnPass->setEchoMode(QLineEdit::Password);
  tnPass->setPlaceholderText("password (or key passphrase)");
  auto * tnKey = new QLineEdit(u8(o.tunnelKeyFile), grpTunnel); tnKey->setPlaceholderText("(optional) SSH private key");
  auto * tnKeyRow = keyRow(&d, tnKey);
  tnForm->addRow(tnOn);
  tnForm->addRow("&Host name:", tnHost);
  tnForm->addRow("Po&rt number:", tnPort);
  tnForm->addRow("&User name:", tnUser);
  tnForm->addRow("Pass&word:", tnPass);
  tnForm->addRow("Private &key:", tnKeyRow);
  auto * tnHint = new QLabel("The session is opened from the tunnel's SSH server. FTP data connections cannot be tunnelled.", grpTunnel);
  tnHint->setWordWrap(true); tnHint->setStyleSheet("color: gray;");
  tnForm->addRow(tnHint);
  auto syncTunnel = [=] {
    bool on = tnOn->isChecked();
    for (QWidget * w : { (QWidget *)tnHost, (QWidget *)tnPort, (QWidget *)tnUser, (QWidget *)tnPass, tnKeyRow }) w->setEnabled(on);
  };
  QObject::connect(tnOn, &QCheckBox::toggled, &d, [=](bool) { syncTunnel(); });
  syncTunnel();
  layTunnel->addWidget(grpTunnel);
  layTunnel->addStretch(1);
  pages->addWidget(pageTunnel); // index 5

  //===========================================================================
  // Pages 6, 7, 8, 9: SSH (Key exchange, Authentication, Bugs)
  //===========================================================================
  for (int pIdx = 6; pIdx <= 9; ++pIdx) {
    auto * pg = new QWidget(&d);
    auto * v = new QVBoxLayout(pg);
    QString title = (pIdx == 6 ? "SSH" : (pIdx == 7 ? "Key exchange" : (pIdx == 8 ? "Authentication" : "Bugs")));
    auto * gb = new QGroupBox(title, pg);
    auto * gbLay = new QVBoxLayout(gb);
    gbLay->addWidget(new QLabel(title + " configuration. Default values match WinSCP / PuTTY SSH protocol engine.", gb));
    gbLay->addStretch(1);
    v->addWidget(gb);
    v->addStretch(1);
    pages->addWidget(pg);
  }

  //===========================================================================
  // Page 10: Note
  //===========================================================================
  auto * pageNote = new QWidget(&d);
  auto * layNote = new QVBoxLayout(pageNote);
  auto * grpNote = new QGroupBox("Session note", pageNote);
  auto * vNote = new QVBoxLayout(grpNote);
  auto * editNote = new QTextEdit(grpNote);
  editNote->setPlaceholderText("Enter notes or documentation for this session here...");
  vNote->addWidget(editNote);
  layNote->addWidget(grpNote);
  pages->addWidget(pageNote); // index 10

  // Selection default: Environment -> Directories (matching WinSCP)
  catTree->setCurrentItem(itemDir);
  QString forcedTab = qEnvironmentVariable("WINSCP_ADVANCED_TAB").toLower();
  if (forcedTab == "connection" || forcedTab == "conn") catTree->setCurrentItem(itemConn);
  else if (forcedTab == "proxy") catTree->setCurrentItem(itemProxy);
  else if (forcedTab == "tunnel") catTree->setCurrentItem(itemTunnel);
  else if (forcedTab == "shell") catTree->setCurrentItem(itemSh);
  else if (forcedTab == "recycle") catTree->setCurrentItem(itemRec);

  // Bottom action bar
  auto * bottomRow = new QHBoxLayout;
  auto * btnColor = new QPushButton("Color \xE2\x96\xBE", &d);
  btnColor->setToolTip("Session tab color");
  bottomRow->addWidget(btnColor);
  bottomRow->addStretch(1);

  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, &d);
  bottomRow->addWidget(bb);
  outer->addLayout(bottomRow);

  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  QObject::connect(bb->button(QDialogButtonBox::Help), &QPushButton::clicked, &d, [&] {
    QDesktopServices::openUrl(QUrl("https://winscp.net/eng/docs/ui_advanced"));
  });
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    if (pxType->currentIndex() != 0 && pxHost->text().trimmed().isEmpty()) {
      catTree->setCurrentItem(itemProxy);
      appMessage(&d, "Proxy", "Please enter the proxy host name.");
      return;
    }
    if (tnOn->isChecked() && tnHost->text().trimmed().isEmpty()) {
      catTree->setCurrentItem(itemTunnel);
      appMessage(&d, "Tunnel", "Please enter the host name of the SSH server to tunnel through.");
      return;
    }
    d.accept();
  });

  if (execDialog(d, "advanced") != QDialog::Accepted) return false;

  // Save back to o
  o.syncBrowsing = cbSyncBrowsing->isChecked();
  o.rememberLastDir = cbRememberLastDir->isChecked();
  o.remoteDir = s8(editRemoteDir->text().trimmed());
  o.localDir = s8(editLocalDir->text().trimmed());
  o.cacheVisitedDirs = cbCacheVisited->isChecked();
  o.cacheDirChanges = cbCacheChanges->isChecked();
  o.permanentCache = cbPermCache->isChecked();
  o.resolveSymlinks = cbResolveSymlinks->isChecked();
  o.followSymlinkDirs = cbFollowSymlinks->isChecked();

  o.ftpPassive = cbPassive->isChecked();
  o.optimizeSendBuf = cbOptBuf->isChecked();
  o.timeout = spinTimeout->value();
  o.pingType = bgKeep->checkedId();
  o.pingInterval = spinPing->value();
  o.addressFamily = bgIp->checkedId();
  o.serverCrLf = cbCrLf->isChecked();
  o.utf8Names = utfCombo->currentIndex();
  o.proxyMethod = pxType->currentIndex();
  o.proxyHost = s8(pxHost->text().trimmed());
  o.proxyPort = pxPort->value();
  o.proxyUser = s8(pxUser->text());
  o.proxyPassword = s8(pxPass->text());
  o.tunnel = tnOn->isChecked();
  o.tunnelHost = s8(tnHost->text().trimmed());
  o.tunnelPort = tnPort->value();
  o.tunnelUser = s8(tnUser->text());
  o.tunnelPassword = s8(tnPass->text());
  o.tunnelKeyFile = s8(tnKey->text().trimmed());
  return true;
}

QString protocolName(engine::Protocol p)
{
  switch (p)
  {
    case engine::Protocol::Scp:    return "SCP";
    case engine::Protocol::Ftp:    return "FTP";
    case engine::Protocol::WebDav: return "WebDAV";
    case engine::Protocol::S3:     return "S3";
    default:                       return "SFTP";
  }
}

QString routeSummary(const engine::ConnectOptions & o)
{
  QStringList parts;
  if (o.tunnel && !o.tunnelHost.empty())
    parts << QString("SSH tunnel via %1%2").arg(o.tunnelUser.empty() ? QString() : u8(o.tunnelUser) + "@", u8(o.tunnelHost));
  if (o.proxyMethod != 0 && !o.proxyHost.empty())
  {
    static const char * names[] = { "", "SOCKS4", "SOCKS5", "HTTP" };
    parts << QString("%1 proxy %2:%3").arg(names[qBound(0, o.proxyMethod, 3)], u8(o.proxyHost)).arg(o.proxyPort);
  }
  if (o.protocol == engine::Protocol::Ftp && !o.ftpPassive) parts << "active mode";
  if (o.utf8Names == 1) parts << "UTF-8 names";
  else if (o.utf8Names == 2) parts << "non-UTF-8 names";
  if (o.serverCrLf && (o.protocol == engine::Protocol::Sftp || o.protocol == engine::Protocol::Scp)) parts << "CR/LF server";
  if (o.syncBrowsing) parts << "Sync browsing";
  if (!o.remoteDir.empty()) parts << QString("Remote: %1").arg(u8(o.remoteDir));
  if (!o.localDir.empty()) parts << QString("Local: %1").arg(u8(o.localDir));
  return parts.join(" \xC2\xB7 ");
}

QString resolveLocalPath(const QString & in)
{
  if (in.trimmed().isEmpty()) return QString();
  QString path = in.trimmed();
  path = QUrl::fromPercentEncoding(path.toUtf8());
  path.replace('\\', '/');

  if (QDir(path).exists() || QFile::exists(path)) return QDir::cleanPath(path);

  QString noDrive = path;
  if (noDrive.length() >= 3 && noDrive[1] == ':' && noDrive[2] == '/') {
    noDrive = noDrive.mid(2);
  }

  int idxX = noDrive.indexOf("ProjectsX", 0, Qt::CaseInsensitive);
  if (idxX >= 0) {
    QString sub = noDrive.mid(idxX);
    QString cand = "/Volumes/DataMac/" + sub;
    if (QDir(cand).exists()) return QDir::cleanPath(cand);
  }

  int idxY = noDrive.indexOf("ProjectsY", 0, Qt::CaseInsensitive);
  if (idxY >= 0) {
    QString sub = noDrive.mid(idxY);
    QString cand = "/Volumes/DataMac/" + sub;
    if (QDir(cand).exists()) return QDir::cleanPath(cand);
  }

  QString candDataMac = "/Volumes/DataMac" + (noDrive.startsWith('/') ? noDrive : "/" + noDrive);
  if (QDir(candDataMac).exists()) return QDir::cleanPath(candDataMac);

  QString candHome = QDir::homePath() + (noDrive.startsWith('/') ? noDrive : "/" + noDrive);
  if (QDir(candHome).exists()) return QDir::cleanPath(candHome);

  if (idxX >= 0) return "/Volumes/DataMac/" + noDrive.mid(idxX);
  if (idxY >= 0) return "/Volumes/DataMac/" + noDrive.mid(idxY);

  return QDir::cleanPath(path);
}

//--- saved sites & workspaces ------------------------------------------------
static QString encodeSiteKey(const QString & name)
{
  QString s = name;
  s.replace('/', "%2F");
  return s;
}

static QString decodeSiteKey(const QString & key)
{
  QString s = key;
  s.replace("%2F", "/");
  return s;
}

void saveSite(const QString & name, const engine::ConnectOptions & o);
static void saveFolder(const QString & folder);

QStringList savedSiteNames()
{
  QSettings s = makeSettings();
  s.beginGroup("sites");
  QStringList raw = s.childGroups();
  s.endGroup();
  QStringList names;
  for (const QString & k : raw) {
    names << decodeSiteKey(k);
  }
  return names;
}

bool loadSite(const QString & name, engine::ConnectOptions & o)
{
  QSettings s = makeSettings();
  QString enc = encodeSiteKey(name);
  if (!s.childGroups().contains("sites")) return false;
  s.beginGroup("sites/" + enc);
  if (s.childKeys().isEmpty()) {
    s.endGroup();
    s.beginGroup("sites/" + name);
    if (s.childKeys().isEmpty()) { s.endGroup(); return false; }
  }
  o = engine::ConnectOptions();
  o.protocol = kProtocols[qBound(0, s.value("protocol", 0).toInt(), 4)];
  o.host = s8(s.value("host").toString());
  o.port = s.value("port", 22).toInt();
  o.user = s8(s.value("user").toString());
  o.password = s8(s.value("pass").toString());
  o.keyFile = s8(s.value("keyFile").toString());
  o.tls = s.contains("tls") ? s.value("tls").toBool()
                            : ((o.protocol == engine::Protocol::WebDav || o.protocol == engine::Protocol::S3) && o.port == 443);
  o.ftpExplicitTls = s.value("ftpExplicitTls", true).toBool();
  o.ftpPassive = s.value("ftpPassive", true).toBool();
  o.serverCrLf = s.value("serverCrLf", false).toBool();
  o.utf8Names = qBound(0, s.value("utf8Names", 0).toInt(), 2);
  o.proxyMethod = qBound(0, s.value("proxyMethod", 0).toInt(), 3);
  o.proxyHost = s8(s.value("proxyHost").toString());
  o.proxyPort = s.value("proxyPort", 0).toInt();
  o.proxyUser = s8(s.value("proxyUser").toString());
  o.proxyPassword = s8(s.value("proxyPass").toString());
  o.tunnel = s.value("tunnel", false).toBool();
  o.tunnelHost = s8(s.value("tunnelHost").toString());
  o.tunnelPort = s.value("tunnelPort", 22).toInt();
  o.tunnelUser = s8(s.value("tunnelUser").toString());
  o.tunnelPassword = s8(s.value("tunnelPass").toString());
  o.tunnelKeyFile = s8(s.value("tunnelKeyFile").toString());
  // Connection / Timeouts / Keepalive / IP version
  o.optimizeSendBuf = s.value("optimizeSendBuf", true).toBool();
  o.timeout = s.value("timeout", 15).toInt();
  o.pingType = s.value("pingType", 0).toInt();
  o.pingInterval = s.value("pingInterval", 30).toInt();
  o.addressFamily = s.value("addressFamily", 0).toInt();
  // Directories & Sync Browsing
  o.syncBrowsing = s.value("syncBrowsing", false).toBool();
  o.rememberLastDir = s.value("rememberLastDir", false).toBool();
  o.remoteDir = s8(s.value("remoteDir").toString());
  o.localDir = s8(s.value("localDir").toString());
  o.cacheVisitedDirs = s.value("cacheVisitedDirs", true).toBool();
  o.cacheDirChanges = s.value("cacheDirChanges", true).toBool();
  o.permanentCache = s.value("permanentCache", true).toBool();
  o.resolveSymlinks = s.value("resolveSymlinks", true).toBool();
  o.followSymlinkDirs = s.value("followSymlinkDirs", false).toBool();
  s.endGroup();
  return true;
}

void saveSite(const QString & name, const engine::ConnectOptions & o)
{
  QSettings s = makeSettings();
  QString enc = encodeSiteKey(name);
  s.remove("sites/" + enc);
  s.remove("sites/" + name);
  s.beginGroup("sites/" + enc);
  s.setValue("protocol", protocolIndex(o.protocol));
  s.setValue("host", u8(o.host));
  s.setValue("port", o.port);
  s.setValue("user", u8(o.user));
  s.setValue("pass", u8(o.password));
  s.setValue("keyFile", u8(o.keyFile));
  s.setValue("tls", o.tls);
  s.setValue("ftpExplicitTls", o.ftpExplicitTls);
  s.setValue("ftpPassive", o.ftpPassive);
  s.setValue("serverCrLf", o.serverCrLf);
  s.setValue("utf8Names", o.utf8Names);
  s.setValue("proxyMethod", o.proxyMethod);
  s.setValue("proxyHost", u8(o.proxyHost));
  s.setValue("proxyPort", o.proxyPort);
  s.setValue("proxyUser", u8(o.proxyUser));
  s.setValue("proxyPass", u8(o.proxyPassword));
  s.setValue("tunnel", o.tunnel);
  s.setValue("tunnelHost", u8(o.tunnelHost));
  s.setValue("tunnelPort", o.tunnelPort);
  s.setValue("tunnelUser", u8(o.tunnelUser));
  s.setValue("tunnelPass", u8(o.tunnelPassword));
  s.setValue("tunnelKeyFile", u8(o.tunnelKeyFile));
  // Connection / Timeouts / Keepalive / IP version
  s.setValue("optimizeSendBuf", o.optimizeSendBuf);
  s.setValue("timeout", o.timeout);
  s.setValue("pingType", o.pingType);
  s.setValue("pingInterval", o.pingInterval);
  s.setValue("addressFamily", o.addressFamily);
  // Directories & Sync Browsing
  s.setValue("syncBrowsing", o.syncBrowsing);
  s.setValue("rememberLastDir", o.rememberLastDir);
  s.setValue("remoteDir", u8(o.remoteDir));
  s.setValue("localDir", u8(o.localDir));
  s.setValue("cacheVisitedDirs", o.cacheVisitedDirs);
  s.setValue("cacheDirChanges", o.cacheDirChanges);
  s.setValue("permanentCache", o.permanentCache);
  s.setValue("resolveSymlinks", o.resolveSymlinks);
  s.setValue("followSymlinkDirs", o.followSymlinkDirs);
  s.endGroup();

  if (name.contains('/')) {
    QString f = name.section('/', 0, -2);
    QStringList folders = s.value("folders/list").toStringList();
    if (!folders.contains(f)) {
      folders.append(f);
      s.setValue("folders/list", folders);
    }
  }
  s.sync();
}

void deleteSite(const QString & name)
{
  QSettings s = makeSettings();
  s.remove("sites/" + encodeSiteKey(name));
  s.remove("sites/" + name);
  s.sync();
}

static QStringList savedFolders()
{
  QSettings s = makeSettings();
  return s.value("folders/list").toStringList();
}

static void saveFolder(const QString & folder)
{
  QSettings s = makeSettings();
  QStringList fl = s.value("folders/list").toStringList();
  if (!fl.contains(folder)) {
    fl.append(folder);
    s.setValue("folders/list", fl);
    s.sync();
  }
}

static void deleteFolder(const QString & folder)
{
  QSettings s = makeSettings();
  QStringList fl = s.value("folders/list").toStringList();
  fl.removeAll(folder);
  s.setValue("folders/list", fl);
  for (const QString & sName : savedSiteNames()) {
    if (sName.startsWith(folder + "/")) {
      deleteSite(sName);
    }
  }
  s.sync();
}

static QStringList savedWorkspaceNames()
{
  QSettings s = makeSettings();
  s.beginGroup("workspaces");
  QStringList names = s.childGroups();
  s.endGroup();
  return names;
}

static QStringList loadWorkspaceSites(const QString & wsName)
{
  QSettings s = makeSettings();
  return s.value("workspaces/" + wsName + "/sites").toStringList();
}

static void saveWorkspace(const QString & wsName, const QStringList & sites)
{
  QSettings s = makeSettings();
  s.setValue("workspaces/" + wsName + "/sites", sites);
  s.sync();
}

static void deleteWorkspace(const QString & wsName)
{
  QSettings s = makeSettings();
  s.remove("workspaces/" + wsName);
  s.sync();
}

// WinSCP password decryptor (reverses WinSCP simple password encryption)
QString decryptWinScpPassword(const QString & encryptedHex, const QString & key)
{
  if (encryptedHex.isEmpty()) return QString();
  const unsigned char magic = 0xA3;
  const unsigned char flagVal = 0xFF;

  int pos = 0;
  auto nextChar = [&]() -> int {
    if (pos + 2 > encryptedHex.length()) return 0;
    bool ok = false;
    int byteVal = encryptedHex.mid(pos, 2).toInt(&ok, 16);
    pos += 2;
    if (!ok) return 0;
    return (~(byteVal ^ magic)) & 0xFF;
  };

  int flag = nextChar();
  int length = -1;
  if (flag == flagVal) {
    int version = nextChar();
    if (version == 0x00) {
      length = nextChar();
    } else if (version == 0x02) {
      length = (nextChar() << 8) + nextChar();
    } else {
      return QString();
    }
  } else {
    length = flag;
  }

  if (length < 0) return QString();

  int shift = nextChar();
  pos += shift * 2;

  QByteArray resBytes;
  for (int i = 0; i < length; ++i) {
    resBytes.append(static_cast<char>(nextChar()));
  }

  QString res = QString::fromUtf8(resBytes);
  if (flag == flagVal) {
    if (res.startsWith(key)) {
      return res.mid(key.length());
    } else {
      return QString();
    }
  }
  return res;
}

int importWinScpIniFile(const QString & filePath, QString * outError)
{
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (outError) *outError = "Could not open file: " + filePath;
    return 0;
  }

  QTextStream in(&f);
  QString currentSection;
  QMap<QString, QMap<QString, QString>> sessions;

  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.startsWith('[') && line.endsWith(']')) {
      QString sec = line.mid(1, line.length() - 2);
      if (sec.startsWith("Sessions/") || sec.startsWith("Sessions\\")) {
        currentSection = sec.mid(9);
      } else {
        currentSection.clear();
      }
    } else if (!currentSection.isEmpty() && line.contains('=')) {
      int eq = line.indexOf('=');
      QString k = line.left(eq).trimmed();
      QString v = line.mid(eq + 1).trimmed();
      sessions[currentSection][k] = v;
    }
  }

  int importedSites = 0;
  QMap<QString, QStringList> workspaces;

  for (auto it = sessions.begin(); it != sessions.end(); ++it) {
    QString rawName = it.key();
    QString unquotedName = QUrl::fromPercentEncoding(rawName.toUtf8());
    const auto & map = it.value();

    // Workspace item
    if (map.value("IsWorkspace") == "1" || map.contains("Link")) {
      QString wsName = unquotedName.section('/', 0, 0);
      QString link = QUrl::fromPercentEncoding(map.value("Link").toUtf8());
      if (!wsName.isEmpty() && !link.isEmpty()) {
        workspaces[wsName].append(link);
      }
      continue;
    }

    // Folder entry
    if (map.value("FS") == "1") {
      saveFolder(unquotedName);
      continue;
    }

    QString host = map.value("HostName");
    if (host.isEmpty()) continue;

    engine::ConnectOptions o;
    o.host = s8(host);
    o.port = map.value("PortNumber", "22").toInt();
    o.user = s8(map.value("UserName"));

    // Password
    if (map.contains("PasswordPlain")) {
      o.password = s8(map.value("PasswordPlain"));
    } else if (map.contains("Password")) {
      QString dec = decryptWinScpPassword(map.value("Password"), map.value("UserName") + host);
      o.password = s8(dec);
    }

    // Protocol
    int proto = map.value("FSProtocol", "0").toInt();
    if (proto == 0) o.protocol = engine::Protocol::Sftp;
    else if (proto == 1) o.protocol = engine::Protocol::Scp;
    else if (proto == 5) o.protocol = engine::Protocol::Ftp;
    else if (proto == 6) o.protocol = engine::Protocol::WebDav;
    else if (proto == 7) o.protocol = engine::Protocol::S3;

    // Tunnel
    o.tunnel = (map.value("Tunnel", "0") == "1") || map.contains("TunnelHostName");
    o.tunnelHost = s8(map.value("TunnelHostName"));
    o.tunnelPort = map.value("TunnelPortNumber", "22").toInt();
    o.tunnelUser = s8(map.value("TunnelUserName"));
    if (map.contains("TunnelPasswordPlain")) {
      o.tunnelPassword = s8(map.value("TunnelPasswordPlain"));
    } else if (map.contains("TunnelPassword")) {
      QString dec = decryptWinScpPassword(map.value("TunnelPassword"), map.value("TunnelUserName") + map.value("TunnelHostName"));
      o.tunnelPassword = s8(dec);
    }
    o.tunnelKeyFile = s8(map.value("TunnelPublicKeyFile"));

    // Proxy
    int pxMethod = map.value("ProxyMethod", "0").toInt();
    o.proxyMethod = pxMethod;
    o.proxyHost = s8(map.value("ProxyHost"));
    o.proxyPort = map.value("ProxyPort", "1080").toInt();
    o.proxyUser = s8(map.value("ProxyUsername"));
    if (map.contains("ProxyPassword")) {
      QString dec = decryptWinScpPassword(map.value("ProxyPassword"), map.value("ProxyUsername") + map.value("ProxyHost"));
      o.proxyPassword = s8(dec);
    }

    o.keyFile = s8(map.value("PublicKeyFile"));

    // Connection / Timeouts / Keepalive / IP version
    o.optimizeSendBuf = (map.value("SendBuf", "262144").toInt() > 0);
    if (map.contains("Timeout")) o.timeout = map.value("Timeout").toInt();
    if (map.contains("PingType")) o.pingType = map.value("PingType").toInt();
    if (map.contains("PingInterval")) o.pingInterval = map.value("PingInterval").toInt();
    if (map.contains("PingIntervalSecs")) o.pingInterval = map.value("PingIntervalSecs").toInt();
    if (map.contains("AddressFamily")) o.addressFamily = map.value("AddressFamily").toInt();
    if (map.contains("FtpPasvMode")) o.ftpPassive = (map.value("FtpPasvMode") == "1");
    // Directories & Sync Browsing
    if (map.contains("RemoteDirectory")) {
      o.remoteDir = s8(QUrl::fromPercentEncoding(map.value("RemoteDirectory").toUtf8()));
    }
    if (map.contains("LocalDirectory")) {
      o.localDir = s8(QUrl::fromPercentEncoding(map.value("LocalDirectory").toUtf8()));
    }
    o.syncBrowsing = (map.value("SynchronizeBrowsing", "0") == "1") || (map.value("SyncBrowsing", "0") == "1");
    o.rememberLastDir = (map.value("RememberLastDir", "0") == "1");
    o.cacheVisitedDirs = (map.value("CacheVisitedDirs", "1") == "1");
    o.cacheDirChanges = (map.value("CacheDirectoryChanges", "1") == "1");
    o.permanentCache = (map.value("PermanentCache", "1") == "1");
    o.resolveSymlinks = (map.value("ResolveSymlinks", "1") == "1");
    o.followSymlinkDirs = (map.value("FollowSymlinks", "0") == "1");

    saveSite(unquotedName, o);
    importedSites++;
  }

  for (auto it = workspaces.begin(); it != workspaces.end(); ++it) {
    saveWorkspace(it.key(), it.value());
  }

  return importedSites;
}

static int importWinScpIni(QWidget * parent)
{
  QString initialDir = "/Volumes/DataMac/Programs/WinSCP-6.5.1-Portable";
  if (!QDir(initialDir).exists()) initialDir = QDir::homePath();
  QString path = QFileDialog::getOpenFileName(parent, "Import Sites from WinSCP.ini",
                                              initialDir, "WinSCP INI (*.ini *.cfg);;All Files (*)");
  if (path.isEmpty()) return 0;

  QString err;
  int count = importWinScpIniFile(path, &err);
  if (!err.isEmpty()) {
    QMessageBox::warning(parent, "Import", err);
    return 0;
  }
  QMessageBox::information(parent, "Import WinSCP Sites",
                           QString("Successfully imported %1 site(s) and folders from WinSCP.ini.").arg(count));
  return count;
}

//--- the dialog ---------------------------------------------------------------
LoginParams showLoginDialog(QWidget * parent, const engine::ConnectOptions * preset)
{
  LoginParams p;
  engine::ConnectOptions adv;
  if (preset != nullptr) adv = *preset;

  QDialog dlg(parent);
  dlg.setWindowTitle("Login");
  dlg.resize(850, 520);
  dlg.setSizeGripEnabled(true);

  auto * outer = new QVBoxLayout(&dlg);
  outer->setContentsMargins(12, 12, 12, 12);
  outer->setSpacing(10);

  auto * mainSplitter = new QSplitter(Qt::Horizontal, &dlg);
  outer->addWidget(mainSplitter, 1);

  // Left Column
  auto * leftPane = new QWidget(&dlg);
  auto * leftCol = new QVBoxLayout(leftPane);
  leftCol->setContentsMargins(0, 0, 0, 0);
  leftCol->setSpacing(6);

  auto * sites = new QTreeWidget(leftPane);
  sites->setHeaderHidden(true);
  sites->setMinimumWidth(220);
  sites->setIndentation(16);
  sites->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  sites->setTextElideMode(Qt::ElideNone);
  leftCol->addWidget(sites, 1);

  // Search filter row
  auto * searchRow = new QHBoxLayout;
  searchRow->setSpacing(4);
  auto * btnPrev = new QToolButton(leftPane); btnPrev->setText("<");
  btnPrev->setToolTip("Previous match");
  auto * editSearch = new QLineEdit(leftPane); editSearch->setPlaceholderText("Filter sites...");
  editSearch->setClearButtonEnabled(true);
  auto * btnNext = new QToolButton(leftPane); btnNext->setText(">");
  btnNext->setToolTip("Next match");
  searchRow->addWidget(btnPrev);
  searchRow->addWidget(editSearch, 1);
  searchRow->addWidget(btnNext);
  leftCol->addLayout(searchRow);

  // Tools & Manage buttons row
  auto * actionRow = new QHBoxLayout;
  actionRow->setSpacing(8);
  auto * btnTools = new QPushButton("Tools", leftPane);
  btnTools->setFixedHeight(26);
  auto * btnManage = new QPushButton("Manage", leftPane);
  btnManage->setFixedHeight(26);
  actionRow->addWidget(btnTools, 1);
  actionRow->addWidget(btnManage, 1);
  leftCol->addLayout(actionRow);

  mainSplitter->addWidget(leftPane);

  // Right Column: QStackedWidget
  auto * stack = new QStackedWidget(&dlg);
  mainSplitter->addWidget(stack);
  mainSplitter->setCollapsible(0, false);
  mainSplitter->setCollapsible(1, false);
  mainSplitter->setStretchFactor(0, 0);
  mainSplitter->setStretchFactor(1, 1);

  // Page 0: Session Page
  auto * sessionPage = new QWidget(&dlg);
  auto * sessionLay = new QVBoxLayout(sessionPage);
  sessionLay->setContentsMargins(0, 0, 0, 0);

  auto * session = new QGroupBox("Session", sessionPage);
  auto * grid = new QGridLayout(session);
  grid->setContentsMargins(12, 14, 12, 12);
  grid->setSpacing(8);

  auto * proto = new QComboBox;
  proto->addItem("SFTP"); proto->addItem("SCP"); proto->addItem("FTP");
  proto->addItem("WebDAV"); proto->addItem("Amazon S3");
  auto * enc = new QComboBox;
  auto * encLabel = new QLabel("Encryption:");
  auto * host = new QLineEdit; host->setPlaceholderText("hostname or IP");
  auto * port = new QSpinBox; port->setRange(1, 65535); port->setValue(22);
  auto * user = new QLineEdit; user->setPlaceholderText("username");
  auto * pass = new QLineEdit; pass->setEchoMode(QLineEdit::Password); pass->setPlaceholderText("password (or key passphrase)");
  auto * keyEdit = new QLineEdit; keyEdit->setPlaceholderText("(optional) SSH private key");
  auto * keyLabel = new QLabel("Private key:");
  auto * keyWidget = keyRow(&dlg, keyEdit);

  grid->addWidget(new QLabel("File protocol:"), 0, 0);
  grid->addWidget(proto, 0, 1);
  grid->addWidget(encLabel, 0, 2);
  grid->addWidget(enc, 0, 3);
  grid->addWidget(new QLabel("Host name:"), 1, 0);
  grid->addWidget(host, 1, 1);
  grid->addWidget(new QLabel("Port number:"), 1, 2);
  grid->addWidget(port, 1, 3);
  grid->addWidget(new QLabel("User name:"), 2, 0);
  grid->addWidget(user, 2, 1);
  grid->addWidget(new QLabel("Password:"), 2, 2);
  grid->addWidget(pass, 2, 3);
  grid->addWidget(keyLabel, 3, 0);
  grid->addWidget(keyWidget, 3, 1, 1, 3);

  auto * advBtn = new QPushButton("&Advanced...");
  auto * advSummary = new QLabel; advSummary->setStyleSheet("color: gray;"); advSummary->setWordWrap(true);
  grid->addWidget(advBtn, 4, 0);
  grid->addWidget(advSummary, 4, 1, 1, 3);
  grid->setRowStretch(5, 1);
  grid->setColumnStretch(1, 3); grid->setColumnStretch(3, 2);

  sessionLay->addWidget(session);
  stack->addWidget(sessionPage);

  // Page 1: Workspace Page
  auto * wsPage = new QWidget(&dlg);
  auto * wsLay = new QVBoxLayout(wsPage);
  wsLay->setContentsMargins(0, 0, 0, 0);

  auto * wsGroup = new QGroupBox("Workspace", wsPage);
  auto * wsForm = new QVBoxLayout(wsGroup);
  wsForm->setContentsMargins(12, 14, 12, 12);
  wsForm->setSpacing(8);

  auto * wsNameRow = new QHBoxLayout;
  wsNameRow->addWidget(new QLabel("Name:"));
  auto * editWsName = new QLineEdit;
  wsNameRow->addWidget(editWsName, 1);
  wsForm->addLayout(wsNameRow);

  auto * wsSitesList = new QListWidget;
  wsSitesList->setAlternatingRowColors(true);
  wsForm->addWidget(new QLabel("Sites in this workspace:"));
  wsForm->addWidget(wsSitesList, 1);

  auto * wsBtnRow = new QHBoxLayout;
  auto * btnAddWsSite = new QPushButton("+ Add Site...");
  auto * btnRemoveWsSite = new QPushButton("- Remove");
  wsBtnRow->addWidget(btnAddWsSite);
  wsBtnRow->addWidget(btnRemoveWsSite);
  wsBtnRow->addStretch(1);
  wsForm->addLayout(wsBtnRow);

  wsLay->addWidget(wsGroup);
  stack->addWidget(wsPage);

  // Protocols & Encryption sync
  auto currentProtocol = [&]() { return kProtocols[qBound(0, proto->currentIndex(), 4)]; };
  auto currentTls = [&]() { return enc->currentIndex() > 0; };
  auto currentFtpExplicit = [&]() { return enc->currentIndex() != 2; };
  auto refreshSummary = [&] {
    engine::ConnectOptions tmp = adv; tmp.protocol = currentProtocol();
    QString sum = routeSummary(tmp);
    advSummary->setText(sum.isEmpty() ? "Direct connection" : sum);
  };

  bool loading = false;
  auto applyProtocol = [&](engine::Protocol pr, bool tls, bool ftpExplicit) {
    enc->blockSignals(true);
    enc->clear();
    enc->addItem("No encryption");
    if (pr == engine::Protocol::Ftp) { enc->addItem("TLS/SSL Explicit encryption"); enc->addItem("TLS/SSL Implicit encryption"); }
    else if (!isSsh(pr)) enc->addItem("TLS/SSL");
    enc->setCurrentIndex(!tls ? 0 : (pr == engine::Protocol::Ftp && !ftpExplicit) ? 2 : 1);
    enc->blockSignals(false);
    encLabel->setVisible(!isSsh(pr)); enc->setVisible(!isSsh(pr));
    keyLabel->setVisible(isSsh(pr)); keyWidget->setVisible(isSsh(pr));
  };
  QObject::connect(proto, &QComboBox::currentIndexChanged, &dlg, [&](int) {
    engine::Protocol pr = currentProtocol();
    const bool tls = (pr == engine::Protocol::S3);
    applyProtocol(pr, tls, true);
    if (!loading) port->setValue(defaultPort(pr, tls, true));
    refreshSummary();
  });
  QObject::connect(enc, &QComboBox::currentIndexChanged, &dlg, [&](int) {
    if (loading) return;
    engine::Protocol pr = currentProtocol();
    const int ports[] = { defaultPort(pr, false, true), defaultPort(pr, true, true), defaultPort(pr, true, false) };
    if (port->value() == ports[0] || port->value() == ports[1] || port->value() == ports[2])
      port->setValue(defaultPort(pr, currentTls(), currentFtpExplicit()));
  });

  auto setForm = [&](const engine::ConnectOptions & o) {
    loading = true;
    proto->setCurrentIndex(protocolIndex(o.protocol));
    applyProtocol(o.protocol, o.tls, o.ftpExplicitTls);
    host->setText(u8(o.host));
    port->setValue(o.port > 0 ? o.port : defaultPort(o.protocol, o.tls, o.ftpExplicitTls));
    user->setText(u8(o.user));
    pass->setText(u8(o.password));
    keyEdit->setText(u8(o.keyFile));
    adv = o;
    loading = false;
    refreshSummary();
  };
  auto getForm = [&]() {
    engine::ConnectOptions o = adv;
    o.protocol = currentProtocol();
    o.host = s8(host->text().trimmed());
    o.port = port->value();
    o.user = s8(user->text());
    o.password = s8(pass->text());
    o.keyFile = isSsh(o.protocol) ? s8(keyEdit->text().trimmed()) : std::string();
    o.tls = !isSsh(o.protocol) && currentTls();
    o.ftpExplicitTls = currentFtpExplicit();
    return o;
  };

  QObject::connect(advBtn, &QPushButton::clicked, &dlg, [&] {
    engine::ConnectOptions tmp = getForm();
    if (showAdvancedDialog(&dlg, tmp)) { adv = tmp; refreshSummary(); }
  });

  // Tree management
  sites->setIconSize(QSize(18, 18));
  QTreeWidgetItem * newSite = nullptr;
  auto reloadSites = [&] {
    sites->clear();
    newSite = new QTreeWidgetItem(sites);
    newSite->setText(0, "New Site");
    newSite->setIcon(0, QIcon(":/newsite.png"));
    newSite->setData(0, Qt::UserRole, "");
    newSite->setData(0, Qt::UserRole + 1, "new_site");

    // Workspaces
    for (const QString & ws : savedWorkspaceNames()) {
      auto * it = new QTreeWidgetItem(sites);
      it->setText(0, ws);
      it->setIcon(0, QIcon(":/workspace.png"));
      it->setData(0, Qt::UserRole, ws);
      it->setData(0, Qt::UserRole + 1, "workspace");
      it->setToolTip(0, ws);
    }

    // Folders & Sites
    QMap<QString, QTreeWidgetItem*> folderMap;
    std::function<QTreeWidgetItem*(const QString &)> getFolder = [&](const QString & fPath) -> QTreeWidgetItem* {
      if (folderMap.contains(fPath)) return folderMap[fPath];
      QTreeWidgetItem * parent = nullptr;
      if (fPath.contains('/')) {
        parent = getFolder(fPath.section('/', 0, -2));
      }
      auto * fIt = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(sites);
      fIt->setText(0, fPath.section('/', -1));
      fIt->setIcon(0, QIcon(":/folder.png"));
      fIt->setData(0, Qt::UserRole, fPath);
      fIt->setData(0, Qt::UserRole + 1, "folder");
      fIt->setToolTip(0, fPath);
      fIt->setExpanded(true);
      folderMap[fPath] = fIt;
      return fIt;
    };

    for (const QString & f : savedFolders()) {
      getFolder(f);
    }

    // Sites
    QStringList allSites = savedSiteNames();
    for (const QString & sName : allSites) {
      if (sName.contains('/')) {
        QString f = sName.section('/', 0, -2);
        QString base = sName.section('/', -1);
        QTreeWidgetItem * pFolder = getFolder(f);
        auto * it = new QTreeWidgetItem(pFolder);
        it->setText(0, base);
        it->setIcon(0, QIcon(":/site.png"));
        it->setData(0, Qt::UserRole, sName);
        it->setData(0, Qt::UserRole + 1, "site");
        it->setToolTip(0, sName);
      } else {
        auto * it = new QTreeWidgetItem(sites);
        it->setText(0, sName);
        it->setIcon(0, QIcon(":/site.png"));
        it->setData(0, Qt::UserRole, sName);
        it->setData(0, Qt::UserRole + 1, "site");
        it->setToolTip(0, sName);
      }
    }
  };
  reloadSites();

  // Search filter
  auto doFilter = [&](const QString & filter) {
    QString q = filter.trimmed().toLower();
    std::function<bool(QTreeWidgetItem*)> filterItem = [&](QTreeWidgetItem * it) -> bool {
      bool childMatches = false;
      for (int i = 0; i < it->childCount(); ++i) {
        if (filterItem(it->child(i))) childMatches = true;
      }
      bool selfMatches = it->text(0).toLower().contains(q);
      bool visible = q.isEmpty() || selfMatches || childMatches;
      it->setHidden(!visible);
      if (childMatches) it->setExpanded(true);
      return visible;
    };
    for (int i = 0; i < sites->topLevelItemCount(); ++i) {
      filterItem(sites->topLevelItem(i));
    }
  };
  QObject::connect(editSearch, &QLineEdit::textChanged, &dlg, doFilter);

  // Bottom action buttons
  auto * btnRow = new QHBoxLayout;
  btnRow->addStretch(1);
  auto * loginBtn = new QPushButton("Login", &dlg); loginBtn->setDefault(true);
  loginBtn->setMinimumWidth(85);
  auto * saveBtn = new QPushButton("Save", &dlg);
  saveBtn->setMinimumWidth(80);
  auto * closeBtn = new QPushButton("Close", &dlg);
  closeBtn->setMinimumWidth(80);
  auto * helpBtn = new QPushButton("Help", &dlg);
  helpBtn->setMinimumWidth(75);
  btnRow->addWidget(loginBtn);
  btnRow->addWidget(saveBtn);
  btnRow->addWidget(closeBtn);
  btnRow->addWidget(helpBtn);
  outer->addLayout(btnRow);

  // Selection change
  QObject::connect(sites, &QTreeWidget::currentItemChanged, &dlg, [&](QTreeWidgetItem * cur, QTreeWidgetItem *) {
    if (cur == nullptr) return;
    QString type = cur->data(0, Qt::UserRole + 1).toString();
    QString val = cur->data(0, Qt::UserRole).toString();

    if (type == "workspace") {
      stack->setCurrentIndex(1);
      editWsName->setText(val);
      wsSitesList->clear();
      QStringList wSites = loadWorkspaceSites(val);
      for (const QString & sName : wSites) {
        auto * it = new QListWidgetItem(sName, wsSitesList);
        it->setIcon(QIcon(":/site.png"));
        wsSitesList->addItem(it);
      }
      loginBtn->setEnabled(true);
      loginBtn->setText("Open");
      loginBtn->setIcon(dlg.style()->standardIcon(QStyle::SP_DirIcon));
      saveBtn->hide();
    } else if (type == "folder") {
      stack->setCurrentIndex(0);
      loginBtn->setText("Login");
      loginBtn->setIcon(QIcon());
      loginBtn->setEnabled(false);
      saveBtn->show();
      setForm(engine::ConnectOptions());
    } else {
      stack->setCurrentIndex(0);
      loginBtn->setText("Login");
      loginBtn->setIcon(QIcon());
      loginBtn->setEnabled(true);
      saveBtn->show();

      if (type == "new_site" || val.isEmpty()) {
        setForm(engine::ConnectOptions());
      } else if (type == "site") {
        engine::ConnectOptions o;
        if (loadSite(val, o)) setForm(o);
      }
    }
  });

  // Workspace buttons
  QObject::connect(btnAddWsSite, &QPushButton::clicked, &dlg, [&] {
    QString curWs = editWsName->text().trimmed();
    if (curWs.isEmpty()) return;
    QStringList all = savedSiteNames();
    if (all.isEmpty()) { QMessageBox::information(&dlg, "Workspace", "No saved sites to add."); return; }
    bool ok = false;
    QString picked = QInputDialog::getItem(&dlg, "Add Site to Workspace", "Select site:", all, 0, false, &ok);
    if (ok && !picked.isEmpty()) {
      QStringList list = loadWorkspaceSites(curWs);
      list.append(picked);
      saveWorkspace(curWs, list);
      auto * it = new QListWidgetItem(picked, wsSitesList);
      it->setIcon(QIcon(":/site.png"));
      wsSitesList->addItem(it);
    }
  });

  QObject::connect(btnRemoveWsSite, &QPushButton::clicked, &dlg, [&] {
    QString curWs = editWsName->text().trimmed();
    auto * item = wsSitesList->currentItem();
    if (!item || curWs.isEmpty()) return;
    QString text = item->text();
    QStringList list = loadWorkspaceSites(curWs);
    list.removeAll(text);
    saveWorkspace(curWs, list);
    delete item;
  });

  // Tools Menu
  auto * menuTools = new QMenu(&dlg);
  menuTools->addAction("Import from WinSCP.ini...", [&] {
    if (importWinScpIni(&dlg) > 0) reloadSites();
  });
  menuTools->addAction("Export Configuration to INI...", [&] {
    QString p = QFileDialog::getSaveFileName(&dlg, "Export Configuration", QDir::homePath() + "/WinSCP.ini", "INI Files (*.ini)");
    if (!p.isEmpty()) {
      QFile::remove(p);
      QFile::copy(portableIniPath(), p);
      QMessageBox::information(&dlg, "Export", "Configuration exported to " + p);
    }
  });
  btnTools->setMenu(menuTools);

  // Manage Menu
  auto * menuManage = new QMenu(&dlg);
  menuManage->addAction("New Site", [&] {
    sites->setCurrentItem(newSite);
    host->setFocus();
  });
  menuManage->addAction("New Folder...", [&] {
    bool ok = false;
    QString f = QInputDialog::getText(&dlg, "New Folder", "Folder name:", QLineEdit::Normal, "", &ok).trimmed();
    if (ok && !f.isEmpty()) {
      saveFolder(f);
      reloadSites();
    }
  });
  menuManage->addAction("New Workspace...", [&] {
    bool ok = false;
    QString w = QInputDialog::getText(&dlg, "New Workspace", "Workspace name:", QLineEdit::Normal, "", &ok).trimmed();
    if (ok && !w.isEmpty()) {
      saveWorkspace(w, QStringList());
      reloadSites();
    }
  });
  menuManage->addSeparator();
  menuManage->addAction("Rename...", [&] {
    auto * it = sites->currentItem();
    if (!it) return;
    QString type = it->data(0, Qt::UserRole + 1).toString();
    QString val = it->data(0, Qt::UserRole).toString();
    if (type == "site") {
      bool ok = false;
      QString nw = QInputDialog::getText(&dlg, "Rename Site", "New site name:", QLineEdit::Normal, val, &ok).trimmed();
      if (ok && !nw.isEmpty() && nw != val) {
        engine::ConnectOptions o;
        if (loadSite(val, o)) {
          deleteSite(val);
          saveSite(nw, o);
          reloadSites();
        }
      }
    } else if (type == "folder") {
      bool ok = false;
      QString nw = QInputDialog::getText(&dlg, "Rename Folder", "New folder name:", QLineEdit::Normal, val, &ok).trimmed();
      if (ok && !nw.isEmpty() && nw != val) {
        saveFolder(nw);
        for (const QString & s : savedSiteNames()) {
          if (s.startsWith(val + "/")) {
            engine::ConnectOptions o;
            if (loadSite(s, o)) {
              deleteSite(s);
              saveSite(nw + "/" + s.mid(val.length() + 1), o);
            }
          }
        }
        deleteFolder(val);
        reloadSites();
      }
    }
  });
  menuManage->addAction("Delete", [&] {
    auto * it = sites->currentItem();
    if (!it) return;
    QString type = it->data(0, Qt::UserRole + 1).toString();
    QString val = it->data(0, Qt::UserRole).toString();
    if (type == "site") {
      if (QMessageBox::question(&dlg, "Delete", "Delete site '" + val + "'?") == QMessageBox::Yes) {
        deleteSite(val);
        reloadSites();
        sites->setCurrentItem(newSite);
      }
    } else if (type == "folder") {
      if (QMessageBox::question(&dlg, "Delete", "Delete folder '" + val + "' and all its sites?") == QMessageBox::Yes) {
        deleteFolder(val);
        reloadSites();
        sites->setCurrentItem(newSite);
      }
    } else if (type == "workspace") {
      if (QMessageBox::question(&dlg, "Delete", "Delete workspace '" + val + "'?") == QMessageBox::Yes) {
        deleteWorkspace(val);
        reloadSites();
        sites->setCurrentItem(newSite);
      }
    }
  });
  menuManage->addAction("Clone Site...", [&] {
    auto * it = sites->currentItem();
    if (!it || it->data(0, Qt::UserRole + 1).toString() != "site") return;
    QString val = it->data(0, Qt::UserRole).toString();
    engine::ConnectOptions o;
    if (loadSite(val, o)) {
      QString cl = val + " (Copy)";
      saveSite(cl, o);
      reloadSites();
    }
  });
  btnManage->setMenu(menuManage);

  // Save button
  QObject::connect(saveBtn, &QPushButton::clicked, &dlg, [&] {
    engine::ConnectOptions o = getForm();
    auto * it = sites->currentItem();
    QString def;
    if (it) {
      QString type = it->data(0, Qt::UserRole + 1).toString();
      QString val = it->data(0, Qt::UserRole).toString();
      if (type == "folder") def = val + "/" + (o.user.empty() ? "mysite" : u8(o.user) + "@" + u8(o.host));
      else if (type == "site") def = val;
    }
    if (def.isEmpty()) def = u8(o.user.empty() ? "mysite" : o.user) + "@" + u8(o.host);

    bool okIn = false;
    QString name = QInputDialog::getText(&dlg, "Save session", "Site name (use Folder/Site to organize):",
                                         QLineEdit::Normal, def, &okIn).trimmed();
    if (!okIn || name.isEmpty()) return;
    saveSite(name, o);
    reloadSites();
    for (int i = 0; i < sites->topLevelItemCount(); ++i) {
      auto * topIt = sites->topLevelItem(i);
      if (topIt->data(0, Qt::UserRole).toString() == name) { sites->setCurrentItem(topIt); break; }
      for (int c = 0; c < topIt->childCount(); ++c) {
        if (topIt->child(c)->data(0, Qt::UserRole).toString() == name) { sites->setCurrentItem(topIt->child(c)); break; }
      }
    }
  });

  // Double click to login
  QObject::connect(sites, &QTreeWidget::itemDoubleClicked, &dlg, [&](QTreeWidgetItem * it, int) {
    if (!it) return;
    QString type = it->data(0, Qt::UserRole + 1).toString();
    if (type == "site") {
      p.ok = true;
      p.siteName = it->data(0, Qt::UserRole).toString();
      p.o = getForm();
      dlg.accept();
    } else if (type == "workspace") {
      p.ok = true;
      p.siteName = it->data(0, Qt::UserRole).toString();
      p.workspaceSites = loadWorkspaceSites(p.siteName);
      dlg.accept();
    }
  });

  // Login button clicked
  QObject::connect(loginBtn, &QPushButton::clicked, &dlg, [&] {
    auto * it = sites->currentItem();
    QString type = it ? it->data(0, Qt::UserRole + 1).toString() : "site";
    if (type == "folder") return;
    if (type == "workspace") {
      p.ok = true;
      p.siteName = editWsName->text().trimmed();
      p.workspaceSites = loadWorkspaceSites(p.siteName);
      dlg.accept();
    } else {
      p.ok = true;
      p.siteName = it ? it->data(0, Qt::UserRole).toString() : QString();
      p.o = getForm();
      dlg.accept();
    }
  });

  QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
  QObject::connect(helpBtn, &QPushButton::clicked, &dlg, [&] {
    QDesktopServices::openUrl(QUrl("https://winscp.net/eng/docs/start"));
  });

  // Preset initial
  {
    engine::ConnectOptions initial;
    if (preset != nullptr) initial = *preset;
    setForm(initial);
  }
  sites->setCurrentItem(newSite);

  QSettings set = makeSettings();
  if (set.contains("login/geometry")) dlg.restoreGeometry(set.value("login/geometry").toByteArray());
  if (set.contains("login/splitter")) {
    mainSplitter->restoreState(set.value("login/splitter").toByteArray());
  } else {
    mainSplitter->setSizes({330, 520});
  }

  // Scripted runs support
  if (!qEnvironmentVariableIsEmpty("WINSCP_LOGIN_SELECT_SITE")) {
    QString target = qEnvironmentVariable("WINSCP_LOGIN_SELECT_SITE");
    for (int i = 0; i < sites->topLevelItemCount(); ++i) {
      auto * tIt = sites->topLevelItem(i);
      if (tIt->data(0, Qt::UserRole).toString() == target) sites->setCurrentItem(tIt);
      for (int c = 0; c < tIt->childCount(); ++c) {
        if (tIt->child(c)->data(0, Qt::UserRole).toString() == target) sites->setCurrentItem(tIt->child(c));
      }
    }
  }

  const int rc = execDialog(dlg, "login");
  set.setValue("login/geometry", dlg.saveGeometry());
  set.setValue("login/splitter", mainSplitter->saveState());
  set.sync();
  if (!qEnvironmentVariableIsEmpty("WINSCP_LOGIN_SAVE_AS")) {
    saveSite(qEnvironmentVariable("WINSCP_LOGIN_SAVE_AS"), getForm());
  }
  if (rc != QDialog::Accepted) {
    p.ok = false;
    return p;
  }
  return p;
}

//--- authentication prompt ---------------------------------------------------
bool showAuthPrompt(QWidget * parent, const engine::PromptRequest & rq, std::vector<std::string> & answers)
{
  answers.assign(rq.prompts.size(), std::string());

  // '&' marks a keyboard accelerator in the engine's texts ("&Password:"); show plain labels.
  auto plain = [](const std::string & s) { QString t = u8(s); t.replace("&&", "\x01"); t.remove('&'); t.replace("\x01", "&"); return t.trimmed(); };

  if (uiScripted() && qEnvironmentVariableIsEmpty("WINSCP_DIALOG_SHOTS"))
  {
    // Headless: answers come from WINSCP_PROMPT_ANSWERS, consumed across prompts.
    static QStringList queue = qEnvironmentVariable("WINSCP_PROMPT_ANSWERS").split('|', Qt::SkipEmptyParts);
    for (size_t i = 0; i < rq.prompts.size(); ++i)
    {
      fprintf(stderr, "[prompt] %s%s / %s\n", rq.forTunnel ? "(tunnel) " : "", plain(rq.name).toUtf8().constData(),
              plain(rq.prompts[i]).toUtf8().constData());
      if (queue.isEmpty()) return false;
      answers[i] = s8(queue.takeFirst());
    }
    return true;
  }

  QDialog d(parent);
  QString title = plain(rq.name);
  d.setWindowTitle(title.isEmpty() ? QStringLiteral("Authentication") : title);
  d.setMinimumWidth(420);
  auto * lay = new QVBoxLayout(&d);
  if (rq.forTunnel)
  {
    auto * tl = new QLabel("<b>SSH tunnel</b>"); lay->addWidget(tl);
  }
  if (!rq.instructions.empty())
  {
    auto * il = new QLabel(u8(rq.instructions));
    il->setWordWrap(true); il->setTextFormat(Qt::PlainText);
    lay->addWidget(il);
  }
  auto * form = new QFormLayout;
  std::vector<QLineEdit *> edits;
  for (size_t i = 0; i < rq.prompts.size(); ++i)
  {
    auto * e = new QLineEdit;
    if (!rq.echo[i]) e->setEchoMode(QLineEdit::Password);
    QString label = plain(rq.prompts[i]);
    auto * l = new QLabel(label.isEmpty() ? QStringLiteral("Response:") : label);
    l->setTextFormat(Qt::PlainText);       // server-supplied text is never interpreted as markup
    form->addRow(l, e);
    edits.push_back(e);
  }
  lay->addLayout(form);
  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  if (!edits.empty()) edits[0]->setFocus();

  if (execDialog(d, "authprompt") != QDialog::Accepted) return false;
  for (size_t i = 0; i < edits.size(); ++i) answers[i] = s8(edits[i]->text());
  return true;
}
