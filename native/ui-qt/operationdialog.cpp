//---------------------------------------------------------------------------
// operationdialog.cpp — WinSCP-style connecting & transfer progress dialogs.
//---------------------------------------------------------------------------
#include "operationdialog.h"
#include "logindialog.h"
#include "uicommon.h"
#include <QApplication>
#include <QFileInfo>
#include <QStyle>

static QString formatTime(qint64 secs)
{
  if (secs < 0) secs = 0;
  qint64 h = secs / 3600;
  qint64 m = (secs % 3600) / 60;
  qint64 s = secs % 60;
  if (h > 0) return QString::asprintf("%02lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s);
  return QString::asprintf("%02lld:%02lld", (long long)m, (long long)s);
}

#include <QListWidget>

//===========================================================================
// ConnectingDialog — WinSCP replica
//===========================================================================
ConnectingDialog::ConnectingDialog(QWidget * parent, const QString & sessionTitle, const engine::ConnectOptions & o)
  : QDialog(parent)
{
  QString title = sessionTitle.isEmpty() ? QString("%1@%2").arg(u8(o.user), u8(o.host)) : sessionTitle;
  setWindowTitle(title);
  setFixedSize(450, 240);
  setWindowModality(Qt::WindowModal);

  auto * layout = new QHBoxLayout(this);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(16);

  // Left icon: transfer arrows + gold padlock
  auto * iconContainer = new QVBoxLayout;
  m_icon = new QLabel(this);
  QPixmap px(":/connecting.png");
  if (px.isNull()) px = QPixmap(":/posixscp.png");
  if (!px.isNull()) m_icon->setPixmap(px.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  else m_icon->setPixmap(style()->standardIcon(QStyle::SP_DriveNetIcon).pixmap(48, 48));
  iconContainer->addWidget(m_icon);
  iconContainer->addStretch(1);
  layout->addLayout(iconContainer);

  // Right list of status steps
  m_list = new QListWidget(this);
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_list->setStyleSheet(
    "QListWidget { background: transparent; border: none; font-size: 13px; outline: none; } "
    "QListWidget::item { padding: 2px 4px; border: none; } "
    "QListWidget::item:selected { background: transparent; color: palette(text); border: 1px dotted gray; } "
  );
  layout->addWidget(m_list, 1);

  // Initial step based on options
  if (o.tunnel && !o.tunnelHost.empty()) {
    addStep("Opening tunnel...");
  } else {
    addStep("Searching for host...");
  }
}

void ConnectingDialog::addStep(const QString & text)
{
  if (text.trimmed().isEmpty()) return;
  auto * item = new QListWidgetItem(text, m_list);
  m_list->addItem(item);
  m_list->setCurrentItem(item);
  m_list->scrollToBottom();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void ConnectingDialog::setStatus(const QString & text)
{
  addStep(text);
}

//===========================================================================
// TransferProgressDialog
//===========================================================================
TransferProgressDialog::TransferProgressDialog(QWidget * parent, const QString & opName,
                                               int totalFiles, std::shared_ptr<std::atomic<bool>> cancelFlag)
  : QDialog(parent), m_opName(opName), m_totalFiles(totalFiles), m_cancelFlag(cancelFlag)
{
  setWindowTitle(opName);
  setFixedWidth(520);
  setWindowModality(Qt::WindowModal);
  m_timer.start();

  auto * layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 18, 22, 18);
  layout->setSpacing(12);

  // Header
  auto * headerLayout = new QHBoxLayout;
  headerLayout->setSpacing(14);

  m_icon = new QLabel(this);
  QPixmap px(":/posixscp.png");
  if (px.isNull()) px = QPixmap(":/winscp.png");
  if (!px.isNull()) m_icon->setPixmap(px.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  else m_icon->setPixmap(style()->standardIcon(QStyle::SP_ArrowUp).pixmap(44, 44));
  headerLayout->addWidget(m_icon);

  auto * titlesLayout = new QVBoxLayout;
  titlesLayout->setSpacing(3);
  m_lblTitle = new QLabel(QString("<b>%1 in progress\xE2\x80\xA6</b>").arg(opName), this);
  titlesLayout->addWidget(m_lblTitle);

  m_lblFile = new QLabel("File: \xE2\x80\x94", this);
  m_lblFile->setTextInteractionFlags(Qt::TextSelectableByMouse);
  titlesLayout->addWidget(m_lblFile);

  m_lblTarget = new QLabel("Target: \xE2\x80\x94", this);
  m_lblTarget->setStyleSheet("color: gray;");
  m_lblTarget->setTextInteractionFlags(Qt::TextSelectableByMouse);
  titlesLayout->addWidget(m_lblTarget);

  headerLayout->addLayout(titlesLayout, 1);
  layout->addLayout(headerLayout);

  // File Progress Bar
  m_pbFile = new QProgressBar(this);
  m_pbFile->setRange(0, 100);
  m_pbFile->setValue(0);
  m_pbFile->setAlignment(Qt::AlignCenter);
  m_pbFile->setFixedHeight(16);
  layout->addWidget(m_pbFile);

  // Overall Progress Bar (for multiple files)
  if (m_totalFiles > 1)
  {
    m_lblTotalCount = new QLabel(QString("Total progress: 0 of %1 items").arg(m_totalFiles), this);
    layout->addWidget(m_lblTotalCount);

    m_pbTotal = new QProgressBar(this);
    m_pbTotal->setRange(0, m_totalFiles);
    m_pbTotal->setValue(0);
    m_pbTotal->setAlignment(Qt::AlignCenter);
    m_pbTotal->setFixedHeight(12);
    layout->addWidget(m_pbTotal);
  }
  else
  {
    m_lblTotalCount = nullptr;
    m_pbTotal = nullptr;
  }

  // Statistics Grid (Classic WinSCP stats box)
  auto * statsFrame = new QFrame(this);
  statsFrame->setFrameShape(QFrame::StyledPanel);
  statsFrame->setStyleSheet("QFrame { background-color: palette(window); border-radius: 4px; padding: 6px; }");
  auto * grid = new QGridLayout(statsFrame);
  grid->setContentsMargins(8, 6, 8, 6);
  grid->setHorizontalSpacing(24);
  grid->setVerticalSpacing(6);

  grid->addWidget(new QLabel("Transferred:", statsFrame), 0, 0);
  m_lblTransferred = new QLabel("0 KB", statsFrame);
  m_lblTransferred->setAlignment(Qt::AlignRight);
  grid->addWidget(m_lblTransferred, 0, 1);

  grid->addWidget(new QLabel("Transfer speed:", statsFrame), 0, 2);
  m_lblSpeed = new QLabel("0 KB/s", statsFrame);
  m_lblSpeed->setAlignment(Qt::AlignRight);
  grid->addWidget(m_lblSpeed, 0, 3);

  grid->addWidget(new QLabel("Time elapsed:", statsFrame), 1, 0);
  m_lblElapsed = new QLabel("00:00", statsFrame);
  m_lblElapsed->setAlignment(Qt::AlignRight);
  grid->addWidget(m_lblElapsed, 1, 1);

  grid->addWidget(new QLabel("Time left:", statsFrame), 1, 2);
  m_lblTimeLeft = new QLabel("00:00", statsFrame);
  m_lblTimeLeft->setAlignment(Qt::AlignRight);
  grid->addWidget(m_lblTimeLeft, 1, 3);

  layout->addWidget(statsFrame);

  // Buttons: [Background] & [Cancel]
  auto * btnLayout = new QHBoxLayout;
  btnLayout->addStretch(1);

  m_btnBackground = new QPushButton("&Background", this);
  m_btnBackground->setToolTip("Send this transfer to the background queue dock");
  m_btnBackground->setFixedWidth(105);
  btnLayout->addWidget(m_btnBackground);

  m_btnCancel = new QPushButton("&Cancel", this);
  m_btnCancel->setFixedWidth(95);
  btnLayout->addWidget(m_btnCancel);

  layout->addLayout(btnLayout);

  connect(m_btnBackground, &QPushButton::clicked, this, [this]{
    emit backgroundRequested();
    hide();
  });

  connect(m_btnCancel, &QPushButton::clicked, this, [this]{
    if (m_cancelFlag) m_cancelFlag->store(true);
    m_btnCancel->setEnabled(false);
    m_lblTitle->setText("<b>Cancelling transfer\xE2\x80\xA6</b>");
  });
}

void TransferProgressDialog::setCurrentFile(const QString & fileName, const QString & targetDir, int fileIndex)
{
  m_lblFile->setText("<b>File:</b> " + fileName);
  m_lblTarget->setText("<b>Target:</b> " + targetDir);
  if (m_lblTotalCount && m_pbTotal)
  {
    m_lblTotalCount->setText(QString("Total progress: %1 of %2 items").arg(fileIndex).arg(m_totalFiles));
    m_pbTotal->setValue(fileIndex - 1);
  }
}

void TransferProgressDialog::updateProgress(const engine::TransferProgress & tp, int fileIndex)
{
  int pct = tp.total > 0 ? (int)(tp.transferred * 100 / tp.total) : 0;
  m_pbFile->setValue(pct);

  if (tp.total > 0)
    m_lblTransferred->setText(QString("%1 / %2 (%3%)")
                              .arg(u8(engine::formatSize(tp.transferred)), u8(engine::formatSize(tp.total)))
                              .arg(pct));
  else
    m_lblTransferred->setText(u8(engine::formatSize(tp.transferred)));

  m_lblSpeed->setText(tp.cps > 0 ? u8(engine::formatSize(tp.cps)) + "/s" : "0 KB/s");

  qint64 elapsedSecs = m_timer.elapsed() / 1000;
  m_lblElapsed->setText(formatTime(elapsedSecs));

  if (tp.cps > 0 && tp.total > tp.transferred)
  {
    qint64 leftSecs = (tp.total - tp.transferred) / tp.cps;
    m_lblTimeLeft->setText(formatTime(leftSecs));
  }
  else
  {
    m_lblTimeLeft->setText("\xE2\x80\x94");
  }

  if (m_pbTotal && m_totalFiles > 1)
  {
    double overallPct = (double)(fileIndex - 1) + ((double)pct / 100.0);
    m_pbTotal->setValue((int)overallPct);
  }
}

void TransferProgressDialog::finishTransfer()
{
  m_pbFile->setValue(100);
  if (m_pbTotal) m_pbTotal->setValue(m_totalFiles);
}
