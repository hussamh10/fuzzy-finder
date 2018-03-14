#include "finderwindow.h"
#include "ui_finderwindow.h"

#include <windows.h>
#include <QProcess>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QGraphicsDropShadowEffect>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QMenu>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QUrl>
#include <QTextDocument>
#include <QPainter>
#include <QKeyEvent>
#include <QScreen>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>

const QString FinderWindow::SERVERNAME = "fuzzyfinder";

FinderWindow::FinderWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::FinderWindow) {
	ui->setupUi(this);
	localServer = new QLocalServer();
	indexed = false;
	ignoreResults = false;
	resultCount = 0;
}

FinderWindow::~FinderWindow() {
	delete ui;
}

bool FinderWindow::nativeEvent(const QByteArray &eventType, void *message, long *result) {
	Q_UNUSED(eventType);
	Q_UNUSED(result);
	MSG *msg = static_cast<MSG*>(message);
	if (msg->message == WM_HOTKEY) {
		toggleWindow();
		return true;
	}
	return false;
}

bool FinderWindow::isAlreadyRunning() {
	QLocalSocket socket;
	socket.connectToServer(SERVERNAME, QLocalSocket::ReadWrite);
	if (socket.waitForConnected()) {
		return true;
	}
	return false;
}

void FinderWindow::startListening() {
	localServer->removeServer(SERVERNAME);
	localServer->listen(SERVERNAME);
}

void FinderWindow::initUI() {
	setWindowFlags(Qt::Window | Qt::FramelessWindowHint| Qt::WindowStaysOnTopHint | Qt::Popup | Qt::NoDropShadowWindowHint);
	setAttribute(Qt::WA_TranslucentBackground, true);

	QGraphicsDropShadowEffect* searchBarEffect = new QGraphicsDropShadowEffect();
	searchBarEffect->setBlurRadius(10);
	searchBarEffect->setOffset(0,0);
	searchBarEffect->setColor(QColor(0,0,0,200));

	ui->topArea->setGraphicsEffect(searchBarEffect);

	ui->scrollAreaContents->layout()->setAlignment(Qt::AlignTop);
}

void FinderWindow::initTray() {
	trayIcon = new QSystemTrayIcon(this);
	trayIcon->setIcon(QIcon(":/icons/app_icon"));

	QMenu *menu = new QMenu(this);
	QAction *exit = new QAction("Exit", menu);

	connect(exit, SIGNAL(triggered(bool)), this, SLOT(exit()));

	QMenu *themeMenu = new QMenu("Themes", menu);
	QAction *darkTheme = new QAction("Dark", themeMenu);
	QAction *lightTheme = new QAction("Light", themeMenu);

	darkTheme->setProperty("theme", DARK);
	lightTheme->setProperty("theme", LIGHT);

	connect(darkTheme, SIGNAL(triggered(bool)), this, SLOT(setTheme()));
	connect(lightTheme, SIGNAL(triggered(bool)), this, SLOT(setTheme()));

	themeMenu->addAction(darkTheme);
	themeMenu->addAction(lightTheme);

	menu->addMenu(themeMenu);
	menu->addAction(exit);

	trayIcon->setContextMenu(menu);
	trayIcon->show();
	trayIcon->showMessage("Fuzzy Finder", "Fuzzy Finder is indexing your directories.");
}

void FinderWindow::initPyProcess() {
	pyproc = new QProcess(this);
	pyproc->start("python main.py");
	connect(pyproc, SIGNAL(readyReadStandardOutput()), this, SLOT(pyProcOutputAvailable()));
}

void FinderWindow::initLocalServer() {
	connect(localServer, SIGNAL(newConnection()), this, SLOT(newInstance()));
}

void FinderWindow::initIndexer() {
	timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(runIndexer()));
	timer->start(60 * 60 * 1000);
}

QString FinderWindow::getGlobalStyleSheet() {
	QFile file(":/themes/global");
	file.open(QFile::ReadOnly);
	return QString(file.readAll());
}

QString FinderWindow::getThemeFontColor() {
	return Settings::getInstance()->getCurrentTheme() == LIGHT ? "#000,#777" : "#fff,#ccc";
}

QString FinderWindow::getThemedStyleSheet(Theme t) {
	QFile file;
	switch(t) {
	case DARK:
		file.setFileName(":/themes/dark");
		break;
	case LIGHT:
		file.setFileName(":/themes/light");
		break;
	}
	file.open(QFile::ReadOnly);
	return QString(file.readAll());
}

QLabel *FinderWindow::createNrLabel() {
	QLabel *nrLabel = new QLabel(this);
	QString color = getThemeFontColor().split(",")[0];
	nrLabel->setText("<center><font face='Roboto Cn' color="+color+" size=5>No results found.</font></center>");
	return nrLabel;
}

void FinderWindow::initWindowSize() {
	QScreen* screen = QGuiApplication::primaryScreen();
	QRect screenGeometry = screen->geometry();
	setFixedWidth(screenGeometry.width() / 2);
	setGeometry(screenGeometry.width() / 4, screenGeometry.height() / 4, width(), height());
	update();
}

void FinderWindow::setTheme() {
	QVariant v = QObject::sender()->property("theme");
	Theme t = *(Theme*)&v;
	setTheme((Theme)t);
}

void FinderWindow::resetSearch() {
	ignoreResults = true;
	clearResults();
	ui->scrollAreaContents->hide();
	ui->searchBar->clear();
	ui->searchBar->setFocus();
}

void FinderWindow::clearResults() {
	resultCount = 0;
	QLayoutItem* child;
	while ((child = ui->scrollAreaContents->layout()->takeAt(0)) != 0) {
		delete child->widget();
		delete child;
	}
}

void FinderWindow::launch() {
	toggleWindow();
	QDesktopServices::openUrl(QUrl::fromLocalFile(QObject::sender()->property("path").toString()));
}

void FinderWindow::toggleWindow() {
	if (!indexed) {
		trayIcon->showMessage("Fuzzy Finder", "Your directories are currently being indexed. Please wait.");
	} else if (this->isHidden()) {
		resetSearch();
		this->show();
		this->activateWindow();
		ui->searchBar->setFocus();
	} else {
		this->hide();
	}
}

void FinderWindow::search(QString query) {
	pyproc->write(query.toStdString().c_str());
	pyproc->write("\r\n");
}

void FinderWindow::etchButtonText(QPushButton *button, QString &name, QString &path) {
	QTextDocument Text;
	QList<QString> color = getThemeFontColor().split(",");
	Text.setHtml("<font face='Roboto Cn' color="+color[0]+" size=5>" +
				 name +
				 "</font>&nbsp;<font face='Roboto' color="+color[1]+" size=4><i>"+
				 path +
				 "</i></font>");

	// crop so it fits inside the button
	QPixmap pixmap(this->size().width() * 0.9, Text.size().height());
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	Text.drawContents(&painter, pixmap.rect());
	QIcon ButtonIcon(pixmap);
	button->setIcon(ButtonIcon);
	button->setIconSize(pixmap.size());
}

void FinderWindow::setTheme(Theme t) {
	setStyleSheet(getGlobalStyleSheet() + getThemedStyleSheet(t));
	Settings::getInstance()->setCurrentTheme(t);
	Settings::getInstance()->save();
}

void FinderWindow::scrollToTop() {
	ui->scrollArea->verticalScrollBar()->setValue(0);
}

void FinderWindow::scrollToBottom() {
	ui->scrollArea->verticalScrollBar()->setValue(ui->scrollArea->verticalScrollBar()->maximum());
}

void FinderWindow::appendResult(QString name, QString path) {
	QPushButton *button = new QPushButton(ui->scrollAreaContents);
	etchButtonText(button, name, path);
	button->setProperty("path", path);
	button->setDefault(true);
	connect(button, SIGNAL(clicked()), this, SLOT(launch()));
	resultCount++;
	if (resultCount == 1) {
		// first result
		button->setStyleSheet("border-top: none;");
		ui->scrollAreaContents->show();
		// delete 'no result' label
		QLayoutItem *item = ui->scrollAreaContents->layout()->takeAt(0);
		delete item->widget();
		delete item;
	}
	ui->scrollAreaContents->layout()->addWidget(button);
}

void FinderWindow::keyPressEvent(QKeyEvent *e) {
	if (!ui->searchBar->hasFocus()) {
		if (e->key() == Qt::Key_Escape) {
			resetSearch();
		} else {
			ui->searchBar->setFocus();
			// send key event to search bar
			ui->searchBar->event(e);
		}
	} else if (ui->searchBar->hasFocus() && e->key() == Qt::Key_Down) {
		QCoreApplication::postEvent(this, new QKeyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier));
		scrollToTop();
	} else if (ui->searchBar->hasFocus() && e->key() == Qt::Key_Up) {
		QCoreApplication::postEvent(this, new QKeyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::ShiftModifier));
		scrollToBottom();
	} else {
		QMainWindow::keyPressEvent(e);
	}
}

void FinderWindow::pyProcOutputAvailable() {
	while (pyproc->canReadLine()) {
		QString str(pyproc->readLine());
		if (!indexed && str.trimmed() == ":indexed") {
			trayIcon->showMessage("Fuzzy Finder", "Indexing complete. Press Ctrl+Space to open the finder window.");
			indexed = true;
		} else if (str.startsWith(":")) {
			if (str.remove(0,1) == ui->searchBar->text()) {
				// make sure results match the current query
				ignoreResults = false;
			}
			clearResults();
			ui->scrollAreaContents->layout()->addWidget(createNrLabel());
		} else if (indexed && !ignoreResults) {
			QList<QString> list = str.split('|');
			appendResult(list[0], list[1].trimmed());
		}
	}
}

void FinderWindow::runIndexer() {
	(new QProcess(this))->start("python libs/index.py");
}

void FinderWindow::newInstance() {
	toggleWindow();
}

void FinderWindow::exit() {
	QApplication::quit();
}

void FinderWindow::init() {
	initLocalServer();
	initUI();
	initWindowSize();
	initTray();
	initPyProcess();
	initIndexer();

	Settings::getInstance()->load();
	setTheme(Settings::getInstance()->getCurrentTheme());
	connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(initWindowSize()));
	RegisterHotKey(HWND(winId()), 0, MOD_CONTROL, VK_SPACE);
}

void FinderWindow::on_searchBar_returnPressed() {
	if (resultCount > 0) {
		((QPushButton*)ui->scrollAreaContents->layout()->itemAt(0)->widget())->animateClick();
	}
}

void FinderWindow::on_searchBar_textEdited(const QString &arg1) {
	ignoreResults = false;
	QString str = arg1;
	str.remove(QRegularExpression("[\\[\\]~`!@#$%^&*\\(\\);:\"'<>,?/+=-_]"));
	if (str.isEmpty()) {
		resetSearch();
		return;
	}
	scrollToTop();
	ui->searchBar->setText(str);
	search(str);
}
