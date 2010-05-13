/**
 * qt4client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * A Qt-4 based universal telephony client
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "qt4client.h"
#include <QtUiTools>

#ifdef _WINDOWS
#define DEFAULT_DEVICE "dsound/*"
#elif defined(__APPLE__)
#define DEFAULT_DEVICE "coreaudio/*"
#elif defined(__linux__)
#define DEFAULT_DEVICE "alsa/default"
#else
#define DEFAULT_DEVICE "oss//dev/dsp"
#endif

namespace TelEngine {

static unsigned int s_allHiddenQuit = 0; // Quit on all hidden notification if this counter is 0

// Factory used to create objects in client's thread
class Qt4ClientFactory : public UIFactory
{
public:
    Qt4ClientFactory(const char* name = "Qt4ClientFactory");
    virtual void* create(const String& type, const char* name, NamedList* params = 0);
};

// Class used for temporary operations on QT widgets
// Keeps a pointer to a widget and its type
// NOTE: The methods of this class don't check the widget pointer
class QtWidget
{
public:
    enum Type {
	PushButton     = 0,
	CheckBox       = 1,
	Table          = 2,
	ListBox        = 3,
	ComboBox       = 4,
	Tab            = 5,
	StackWidget    = 6,
	TextEdit       = 7,
	Label          = 8,
	LineEdit       = 9,
	AbstractButton = 10,
	Slider         = 11,
	ProgressBar    = 12,
	SpinBox        = 13,
	Calendar       = 14,
	Unknown        = 15,             // Unknown type
	Action,                          // QAction descendant
	CustomTable,                     // QtTable descendant
	CustomWidget,                    // QtCustomWidget descendant
	CustomObject,                    // QtCustomObject descendant
	Missing                          // Invalid pointer
    };
    // Set widget from object
    inline QtWidget(QObject* w)
	: m_widget(0), m_action(0), m_object(0), m_type(Missing) {
	    if (!w)
		return;
	    if (w->inherits("QWidget"))
		m_widget = static_cast<QWidget*>(w);
	    else if (w->inherits("QAction"))
		m_action = static_cast<QAction*>(w);
	    m_type = getType();
	}
    // Set widget from object and type
    inline QtWidget(QWidget* w, int t)
	: m_widget(w), m_action(0), m_object(0), m_type(t) {
	    if (!m_widget)
		m_type = Missing;
	}
    // Set widget/action from object and name
    inline QtWidget(QWidget* wid, const String& name)
	: m_widget(0), m_action(0), m_object(0), m_type(Missing) {
	    QString what = QtClient::setUtf8(name);
	    m_widget = qFindChild<QWidget*>(wid,what);
	    if (!m_widget) {
		m_action = qFindChild<QAction*>(wid,what);
		if (!m_action)
		    m_object = qFindChild<QObject*>(wid,what);
	    }
	    m_type = getType();
	}
    inline bool valid() const
	{ return type() != Missing; }
    inline bool invalid() const
	{ return type() == Missing; }
    inline int type() const
	{ return m_type; }
    inline operator QWidget*()
	{ return m_widget; }
    inline bool inherits(const char* classname)
	{ return m_widget && m_widget->inherits(classname); }
    inline bool inherits(Type t)
	{ return inherits(s_types[t]); }
    inline QWidget* widget()
	{ return m_widget; }
    inline QWidget* operator ->()
	{ return m_widget; }
    // Static cast methods
    inline QPushButton* button()
	{ return static_cast<QPushButton*>(m_widget); }
    inline QCheckBox* check()
	{ return static_cast<QCheckBox*>(m_widget); }
    inline QTableWidget* table()
	{ return static_cast<QTableWidget*>(m_widget); }
    inline QListWidget* list()
	{ return static_cast<QListWidget*>(m_widget); }
    inline QComboBox* combo()
	{ return static_cast<QComboBox*>(m_widget); }
    inline QTabWidget* tab()
	{ return static_cast<QTabWidget*>(m_widget); }
    inline QStackedWidget* stackWidget()
	{ return static_cast<QStackedWidget*>(m_widget); }
    inline QTextEdit* textEdit()
	{ return static_cast<QTextEdit*>(m_widget); }
    inline QLabel* label()
	{ return static_cast<QLabel*>(m_widget); }
    inline QLineEdit* lineEdit()
	{ return static_cast<QLineEdit*>(m_widget); }
    inline QAbstractButton* abstractButton()
	{ return static_cast<QAbstractButton*>(m_widget); }
    inline QSlider* slider()
	{ return static_cast<QSlider*>(m_widget); }
    inline QProgressBar* progressBar()
	{ return static_cast<QProgressBar*>(m_widget); }
    inline QSpinBox* spinBox()
	{ return static_cast<QSpinBox*>(m_widget); }
    inline QCalendarWidget* calendar()
	{ return static_cast<QCalendarWidget*>(m_widget); }
    inline QtTable* customTable()
	{ return qobject_cast<QtTable*>(m_widget); }
    inline QtCustomWidget* customWidget()
	{ return qobject_cast<QtCustomWidget*>(m_widget); }
    inline QtCustomObject* customObject()
	{ return qobject_cast<QtCustomObject*>(m_object); }
    inline QAction* action()
	{ return m_action; }

    // Find a combo box item
    inline int findComboItem(const String& item) {
	    QComboBox* c = combo();
	    return c ? c->findText(QtClient::setUtf8(item)) : -1;
	}
    // Add an item to a combo box
    inline bool addComboItem(const String& item, bool atStart) {
	    QComboBox* c = combo();
	    if (!c)
		return false;
	    QString it(QtClient::setUtf8(item));
	    if (atStart)
		c->insertItem(0,it);
	    else
		c->addItem(it);
	    return true;
	}
    // Find a list box item
    inline int findListItem(const String& item) {
	    QListWidget* l = list();
	    if (!l)
		return -1;
	    QString it(QtClient::setUtf8(item));
	    for (int i = l->count(); i >= 0 ; i--) {
		QListWidgetItem* tmp = l->item(i);
		if (tmp && it == tmp->text())
		    return i;
	    }
	    return -1;
	}
    // Add an item to a list box
    inline bool addListItem(const String& item, bool atStart) {
	    QListWidget* l = list();
	    if (!l)
		return false;
	    QString it(QtClient::setUtf8(item));
	    if (atStart)
		l->insertItem(0,it);
	    else
		l->addItem(it);
	    return true;
	}

    int getType() {
	    if (m_widget) {
		String cls = m_widget->metaObject()->className(); 
		for (int i = 0; i < Unknown; i++)
		    if (s_types[i] == cls)
			return i;
		if (customTable())
		    return CustomTable;
		if (customWidget())
		    return CustomWidget;
		return Unknown;
	    }
	    if (m_action && m_action->inherits("QAction"))
		return Action;
	    if (customObject())
		return CustomObject;
	    return Missing;
	}
    static String s_types[Unknown];
protected:
    QWidget* m_widget;
    QAction* m_action;
    QObject* m_object;
    int m_type;
private:
    QtWidget() {}
};

// Class used for temporary operations on QTableWidget objects
// NOTE: The methods of this class don't check the table pointer
class TableWidget : public GenObject
{
public:
    TableWidget(QTableWidget* table, bool tmp = true);
    TableWidget(QWidget* wid, const String& name, bool tmp = true);
    TableWidget(QtWidget& table, bool tmp = true);
    ~TableWidget();
    inline QTableWidget* table()
	{ return m_table; }
    inline bool valid()
	{ return m_table != 0; }
    inline QtTable* customTable()
	{ return (valid()) ? qobject_cast<QtTable*>(m_table) : 0; }
    inline const String& name()
	{ return m_name; }
    inline int rowCount()
	{ return m_table->rowCount(); }
    inline int columnCount()
	{ return m_table->columnCount(); }
    inline void setHeaderText(int col, const char* text) {
	    if (col < columnCount())
		m_table->setHorizontalHeaderItem(col,
		    new QTableWidgetItem(QtClient::setUtf8(text)));
	}
    inline bool getHeaderText(int col, String& dest, bool lower = true) {
    	    QTableWidgetItem* item = m_table->horizontalHeaderItem(col);
	    if (item) {
		QtClient::getUtf8(dest,item->text());
		if (lower)
		    dest.toLower();
	    }
	    return item != 0;
	}
    // Get the current selection's row
    inline int crtRow() {
	    QList<QTableWidgetItem*> items = m_table->selectedItems();
	    if (items.size())
		return items[0]->row();
	    return -1;
	}
    inline void repaint()
	{ m_table->repaint(); }
    inline void addRow(int index)
	{ m_table->insertRow(index); }
    inline void delRow(int index) {
	    if (index >= 0)
		m_table->removeRow(index);
	}
    inline void addColumn(int index, int width = -1, const char* name = 0) {
	    m_table->insertColumn(index);
	    if (width >= 0)
		m_table->setColumnWidth(index,width); 
	    setHeaderText(index,name);
	}
    inline void setImage(int row, int col, const String& image) {
	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item)
		item->setIcon(QIcon(QtClient::setUtf8(image)));
	}
    inline void addCell(int row, int col, const String& value) {
	    QTableWidgetItem* item = new QTableWidgetItem(QtClient::setUtf8(value));
	    m_table->setItem(row,col,item);
	}
    inline void setCell(int row, int col, const String& value, bool addNew = true) {
	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item)
		item->setText(QtClient::setUtf8(value));
	    else if (addNew)
		addCell(row,col,value);
	}
    inline bool getCell(int row, int col, String& dest, bool lower = false) {
    	    QTableWidgetItem* item = m_table->item(row,col);
	    if (item) {
		QtClient::getUtf8(dest,item->text());
		if (lower)
		    dest.toLower();
		return true;
	    }
	    return false;
	}
    inline void setID(int row, const String& value)
	{ setCell(row,0,value); }
    // Add or set a row
    void updateRow(const String& item, const NamedList* data, bool atStart);
    // Update a row from a list of parameters
    void updateRow(int row, const NamedList& data);
    // Find a row by the first's column value. Return -1 if not found 
    int getRow(const String& item);
    // Find a column by its label. Return -1 if not found 
    int getColumn(const String& name, bool caseInsentive = true);
protected:
    void init(bool tmp);
private:
    QTableWidget* m_table;               // The table
    String m_name;                       // Table's name
    int m_sortControl;                   // Flag used to set/reset sorting attribute of the table
};

// Store an UI loaded from file to avoid loading it again
class UIBuffer : public String
{
public:
    inline UIBuffer(const String& name, QByteArray* buf)
	: String(name), m_buffer(buf)
	{ s_uiCache.append(this); }
    inline QByteArray* buffer()
	{ return m_buffer; }
    // Remove from list. Release memory
    virtual void destruct();
    // Return an already loaded UI. Load from file if not found.
    // Add URLs paths when missing
    static UIBuffer* build(const String& name);
    // Find a buffer
    static UIBuffer* find(const String& name);
    // Buffer cache
    static ObjList s_uiCache;
private:
    QByteArray* m_buffer;                // The buffer
};

}; // namespace TelEngine

using namespace TelEngine;

// Dynamic properies
static const String s_propsSave = "_yate_save_props"; // Save properties property name
static const String s_propColWidths = "_yate_col_widths"; // Column widths
static String s_propHHeader = "dynamicHHeader";       // Tables: show/hide the horizontal header
static String s_propAction = "dynamicAction";         // Prefix for properties that would trigger some action
static String s_propWindowFlags = "_yate_windowflags"; // Window flags
static String s_propHideInactive = "dynamicHideOnInactive"; // Hide inactive window
static const String s_yatePropPrefix = "_yate_";      // Yate dynamic properties prefix
//
static Qt4ClientFactory s_qt4Factory;
static Configuration s_cfg;
static Configuration s_save;
ObjList UIBuffer::s_uiCache;

// Values used to configure window title bar and border
static TokenDict s_windowFlags[] = {
    {"title",              Qt::WindowTitleHint},
    {"sysmenu",            Qt::WindowSystemMenuHint},
    {"maximize",           Qt::WindowMaximizeButtonHint},
    {"minimize",           Qt::WindowMinimizeButtonHint},
    {"help",               Qt::WindowContextHelpButtonHint},
    {"stayontop",          Qt::WindowStaysOnTopHint},
    {"frameless",          Qt::FramelessWindowHint},
    {0,0}
};

String QtWidget::s_types[QtWidget::Unknown] = {
    "QPushButton",
    "QCheckBox",
    "QTableWidget",
    "QListWidget",
    "QComboBox",
    "QTabWidget",
    "QStackedWidget",
    "QTextEdit",
    "QLabel",
    "QLineEdit",
    "QAbstractButton",
    "QSlider",
    "QProgressBar",
    "QSpinBox",
    "QCalendarWidget"
};

// Handler for QT library messages
static void qtMsgHandler(QtMsgType type, const char* text)
{
    int dbg = DebugAll;
    switch (type) {
	case QtDebugMsg:
	    dbg = DebugInfo;
	    break;
	case QtWarningMsg:
	    dbg = DebugWarn;
	    break;
	case QtCriticalMsg:
	    dbg = DebugGoOn;
	    break;
	case QtFatalMsg:
	    dbg = DebugFail;
	    break;
    }
    Debug("QT",dbg,"%s",text);
}

// Build a list of parameters from a string
// Return the number of parameters found
static unsigned int str2Params(NamedList& params, const String& buf, char sep = '|')
{
    ObjList* list = 0;
    // Check if we have another separator
    if (buf.startsWith("separator=")) {
	sep = buf.at(10);
	list = buf.substr(11).split(sep,false);
    }
    else
	list = buf.split(sep,false);
    unsigned int n = 0;
    for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	int pos = s->find('=');
	if (pos < 1)
	    continue;
	params.addParam(s->substr(0,pos),s->substr(pos + 1));
	n++;
    }
    TelEngine::destruct(list);
    return n;
}

// Utility: fix QT path separator on Windows
// (display paths using only one separator to the user)
static inline QString fixPathSep(QString str)
{
#ifdef _WINDOWS
    QString tmp = str;
    tmp.replace(QChar('/'),QtClient::setUtf8(Engine::pathSeparator()));
    return tmp;
#else
    return str;
#endif
}

// Utility: find a stacked widget's page with the given name
static int findStackedWidget(QStackedWidget& w, const String& name)
{
    QString n(QtClient::setUtf8(name));
    for (int i = 0; i < w.count(); i++) {
	QWidget* page = w.widget(i);
	if (page && n == page->objectName())
	    return i;
    }
    return -1;
}

// Utility function used to get the name of a control
// The name of the control indicates actions, toggles ...
// The action name alias can contain parameters
static bool translateName(QtWidget& w, String& name, NamedList** params = 0)
{
    if (w.invalid())
	return false;
    if (w.type() != QtWidget::Action)
	QtClient::getIdentity(w.widget(),name);
    else
	QtClient::getIdentity(w.action(),name);
    if (!name)
	return true;
    // Check params
    int pos = name.find('|');
    if (pos < 1)
	return true;
    if (params) {
	*params = new NamedList("");
	if (!str2Params(**params,name.substr(pos + 1)))
	    TelEngine::destruct(*params);
    }
    name = name.substr(0,pos);
    return true;
}

// Utility: raise a select event if a list is empty
inline void raiseSelectIfEmpty(int count, Window* wnd, const String& name)
{
    if (!Client::exiting() && count <= 0 && Client::self())
	Client::self()->select(wnd,name,String::empty());
}

// Add dynamic properties from a list of parameters
// Parameter format:
// property_name:property_type=property_value
static void addDynamicProps(QObject* obj, NamedList& props)
{
    static String typeString = "string";
    static String typeBool = "bool";
    static String typeInt = "int";

    if (!obj)
	return;
    unsigned int n = props.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = props.getParam(i);
	if (!(ns && ns->name()))
	    continue;
	int pos = ns->name().find(':');
	if (pos < 1)
	    continue;

	String prop = ns->name().substr(0,pos);
	String type = ns->name().substr(pos + 1);
	QVariant var;
	if (type == typeString)
	    var.setValue(QString(ns->c_str()));
	else if (type == typeBool)
	    var.setValue(ns->toBoolean());
	else if (type == typeInt)
	    var.setValue(ns->toInteger());

	if (var.type() != QVariant::Invalid) {
	    obj->setProperty(prop,var);
	    DDebug(ClientDriver::self(),DebugAll,
		"Object '%s': added dynamic property %s='%s' type=%s",
		YQT_OBJECT_NAME(obj),prop.c_str(),ns->c_str(),var.typeName());
	}
	else
	    Debug(ClientDriver::self(),DebugStub,
		"Object '%s': dynamic property '%s' type '%s' is not supported",
		YQT_OBJECT_NAME(obj),prop.c_str(),type.c_str());
    }
}

// Find a QSystemTrayIcon child of an object
inline QSystemTrayIcon* findSysTrayIcon(QObject* obj, const char* name)
{
    return qFindChild<QSystemTrayIcon*>(obj,QtClient::setUtf8(name));
}

// Utility used to create an object's property if not found
// Add it to a list of strings
// Return true if the list changed
static bool createProperty(QObject* obj, const char* name, QVariant::Type t,
    QtWindow* wnd, QStringList* list)
{
    if (!obj || TelEngine::null(name))
	return false;
    QVariant var = obj->property(name);
    if (var.type() == QVariant::Invalid)
	obj->setProperty(name,QVariant(t));
    else if (var.type() != t) {
	if (wnd)
	    Debug(QtDriver::self(),DebugNote,
		"Window(%s) child '%s' already has a %s property '%s' [%p]",
		wnd->toString().c_str(),YQT_OBJECT_NAME(obj),var.typeName(),name,wnd);
	return false;
    }
    if (!list)
	return false;
    QString s = QtClient::setUtf8(name);
    if (list->contains(s))
	return false;
    *list << s;
    return true;
}


/**
 * Qt4ClientFactory
 */
Qt4ClientFactory::Qt4ClientFactory(const char* name)
    : UIFactory(name)
{
    m_types.append(new String("QSound"));
}

// Build QSound
void* Qt4ClientFactory::create(const String& type, const char* name, NamedList* params)
{
    if (type == "QSound")
	return new QSound(QtClient::setUtf8(name));
    return 0;
}


/**
 * TableWidget
 */
TableWidget::TableWidget(QTableWidget* table, bool tmp)
    : m_table(table), m_sortControl(-1)
{
    if (!m_table)
	return;
    init(tmp);
}

TableWidget::TableWidget(QWidget* wid, const String& name, bool tmp)
    : m_table(0), m_sortControl(-1)
{
    if (wid)
	m_table = qFindChild<QTableWidget*>(wid,QtClient::setUtf8(name));
    if (!m_table)
	return;
    init(tmp);
}

TableWidget::TableWidget(QtWidget& table, bool tmp)
    : m_table(static_cast<QTableWidget*>((QWidget*)table)), m_sortControl(-1)
{
    if (m_table)
	init(tmp);
}

TableWidget::~TableWidget()
{
    if (!m_table)
	return;
    if (m_sortControl >= 0)
	m_table->setSortingEnabled((bool)m_sortControl);
    m_table->repaint();
}

// Add or set a row
void TableWidget::updateRow(const String& item, const NamedList* data, bool atStart)
{
    int row = getRow(item);
    // Add a new one ?
    if (row < 0) {
	row = atStart ? 0 : rowCount();
	addRow(row);
	setID(row,item);
    }
    // Update
    if (data)
	updateRow(row,*data);
}

// Update a row from a list of parameters
void TableWidget::updateRow(int row, const NamedList& data)
{
    int ncol = columnCount();
    for (int i = 0; i < ncol; i++) {
	String header;
	if (!getHeaderText(i,header))
	    continue;
	NamedString* tmp = data.getParam(header);
	if (tmp)
	    setCell(row,i,*tmp);
	// Set image
	tmp = data.getParam(header + "_image");
	if (tmp)
	    setImage(row,i,*tmp);
    }
    // Init vertical header
    String* rowText = data.getParam("row_text");
    String* rowImg = data.getParam("row_image");
    if (rowText || rowImg) {
	QTableWidgetItem* item = m_table->verticalHeaderItem(row);
	if (!item) {
	    item = new QTableWidgetItem;
	    m_table->setVerticalHeaderItem(row,item);
	}
	if (rowText)
	    item->setText(QtClient::setUtf8(*rowText));
	if (rowImg)
	    item->setIcon(QIcon(QtClient::setUtf8(*rowImg)));
    }
}

// Find a row by the first's column value. Return -1 if not found 
int TableWidget::getRow(const String& item)
{
    int n = rowCount();
    for (int i = 0; i < n; i++) {
	String val;
	if (getCell(i,0,val) && item == val)
	    return i;
    }
    return -1;
}

// Find a column by its label. Return -1 if not found 
int TableWidget::getColumn(const String& name, bool caseInsensitive)
{
    int n = columnCount();
    for (int i = 0; i < n; i++) {
	String val;
	if (!getHeaderText(i,val,false))
	    continue;
	if ((caseInsensitive && (name &= val)) || (!caseInsensitive && name == val))
	    return i;
    }
    return -1;
}

void TableWidget::init(bool tmp)
{
    QtClient::getUtf8(m_name,m_table->objectName());
    if (tmp) {
	m_sortControl = m_table->isSortingEnabled() ? 1 : 0;
	if (m_sortControl)
	    m_table->setSortingEnabled(false);
    }
}

/**
 * UIBuffer
 */
// Remove from list. Release memory
void UIBuffer::destruct()
{
    s_uiCache.remove(this,false);
    if (m_buffer) {
	delete m_buffer;
	m_buffer = 0;
    }
    String::destruct();
}

// Return an already loaded UI. Load from file if not found.
// Add URLs paths when missing
UIBuffer* UIBuffer::build(const String& name)
{
    // Check if already loaded from the same location
    UIBuffer* buf = find(name);
    if (buf)
	return buf;

    // Load
    QFile file(QtClient::setUtf8(name));
    file.open(QIODevice::ReadOnly);
    QByteArray* qArray = new QByteArray;
    *qArray = file.readAll();
    file.close();
    if (!qArray->size()) {
	delete qArray;
	return 0;
    }

    // Add URLs path when missing
    QString path = QDir::fromNativeSeparators(QtClient::setUtf8(name));
    // Truncate after last path separator (lastIndexOf() returns -1 if not found)
    path.truncate(path.lastIndexOf(QString("/")) + 1);
    if (path.size()) {
	int start = 0;
	int end = -1;
	while ((start = qArray->indexOf("url(",end + 1)) > 0) {
	    start += 4;
	    end = qArray->indexOf(")",start);
	    if (end <= start)
		break;
	    // Add
	    int len = end - start;
	    QByteArray tmp = qArray->mid(start,len);
	    if (tmp.indexOf('/') != -1)
	        continue;
	    tmp.insert(0,path);
	    qArray->replace(start,len,tmp);
	}
    }
    return new UIBuffer(name,qArray);
}

// Find a buffer
UIBuffer* UIBuffer::find(const String& name)
{
    ObjList* o = s_uiCache.find(name);
    return o ? static_cast<UIBuffer*>(o->get()) : 0;
}


/**
 * QtWindow
 */
QtWindow::QtWindow()
    : m_x(0), m_y(0), m_width(0), m_height(0),
    m_maximized(false), m_mainWindow(false), m_moving(false)
{
}

QtWindow::QtWindow(const char* name, const char* description, const char* alias, QtWindow* parent)
    : QWidget(parent, Qt::Window),
    Window(alias ? alias : name), m_description(description), m_oldId(name),
    m_x(0), m_y(0), m_width(0), m_height(0),
    m_maximized(false), m_mainWindow(false)
{
    setObjectName(QtClient::setUtf8(m_id));
}

QtWindow::~QtWindow()
{
    // Update all hidden counter for tray icons owned by this window
    QList<QSystemTrayIcon*> trayIcons = qFindChildren<QSystemTrayIcon*>(wndWidget());
    if (trayIcons.size() > 0) {
	if (s_allHiddenQuit >= (unsigned int)trayIcons.size())
	    s_allHiddenQuit -= trayIcons.size();
	else {
	    Debug(QtDriver::self(),DebugFail,
		"QtWindow(%s) destroyed with all hidden counter %u greater then tray icons %d [%p]",
		m_id.c_str(),s_allHiddenQuit,trayIcons.size(),this);
	    s_allHiddenQuit = 0;
	}
    }

    // Save settings
    if (m_saveOnClose) {
	m_maximized = isMaximized();
	s_save.setValue(m_id,"maximized",String::boolText(m_maximized));
	// Don't save position if maximized: keep the old one
	if (!m_maximized) {
	    s_save.setValue(m_id,"x",m_x);
	    s_save.setValue(m_id,"y",m_y);
	    s_save.setValue(m_id,"width",m_width);
	    s_save.setValue(m_id,"height",m_height);
	}
	s_save.setValue(m_id,"visible",m_visible);
	// Set dynamic properties to be saved for native QT objects
	QList<QTableWidget*> tables = qFindChildren<QTableWidget*>(wndWidget());
	for (int i = 0; i < tables.size(); i++) {
	    if (qobject_cast<QtTable*>(tables[i]))
		continue;
	    unsigned int n = tables[i]->columnCount();
	    String widths;
	    for (unsigned int j = 0; j < n; j++)
		widths.append(String(tables[i]->columnWidth(j)),",",true);
	    tables[i]->setProperty(s_propColWidths,QVariant(QtClient::setUtf8(widths)));
	}
	// Save child objects properties
	QList<QObject*> child = qFindChildren<QObject*>(wndWidget());
	for (int i = 0; i < child.size(); i++) {
	    NamedList props("");
	    if (!QtClient::getProperty(child[i],s_propsSave,props))
		continue;
	    unsigned int n = props.length();
	    for (unsigned int j = 0; j < n; j++) {
		NamedString* ns = props.getParam(j);
		if (ns && ns->name())
		    QtClient::saveProperty(child[i],ns->name(),this);
	    }
	}
    }
}

// Set windows title
void QtWindow::title(const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::title(%s) [%p]",text.c_str(),this);
    Window::title(text);
    QWidget::setWindowTitle(QtClient::setUtf8(text));
}

void QtWindow::context(const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::context(%s) [%p]",text.c_str(),this);
    m_context = text;
}

bool QtWindow::setParams(const NamedList& params)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setParams() [%p]",this);

    setUpdatesEnabled(false);
    // Check for custom widget params
    if (params == "customwidget") {
	// Each parameter is a list of parameters for a custom widget
	// Parameter name is the widget's name
	unsigned int n = params.length();
	bool ok = true;
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params.getParam(i);
	    NamedList* nl = static_cast<NamedList*>(ns ? ns->getObject("NamedList") : 0);
	    if (!(nl && ns->name()))
		continue;
	    // Find the widget and set its params
	    QtWidget w(wndWidget(),ns->name());
	    if (w.type() == QtWidget::CustomTable)
		ok = w.customTable()->setParams(*nl) && ok;
	    else if (w.type() == QtWidget::CustomWidget)
		ok = w.customWidget()->setParams(*nl) && ok;
	    else if (w.type() == QtWidget::CustomObject)
		ok = w.customObject()->setParams(*nl) && ok;
	    else
		ok = false;
	}
	setUpdatesEnabled(true);
	return ok;
    }
    // Check for system tray icon params
    if (params == "systemtrayicon") {
	// Each parameter is a list of parameters for a system tray icon
	// Parameter name is the widget's name
	// Parameter value indicates delete/create/set an existing one
	unsigned int n = params.length();
	bool ok = false;
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = params.getParam(i);
	    NamedList* nl = static_cast<NamedList*>(ns ? ns->getObject("NamedList") : 0);
	    if (!(nl && ns->name()))
		continue;

	    QSystemTrayIcon* trayIcon = findSysTrayIcon(wndWidget(),ns->name());
	    // Delete
	    if (ns->null()) {
		if (trayIcon) {
		    // Reactivate program termination when the last window was hidden
		    if (s_allHiddenQuit)
			s_allHiddenQuit--;
		    else
			Debug(QtDriver::self(),DebugFail,
			    "QtWindow(%s) all hidden counter is 0 while deleting '%s' tray icon [%p]",
			    m_id.c_str(),YQT_OBJECT_NAME(trayIcon),this);
		    delete trayIcon;
		}
		continue;
	    }
	    // Create a new one
	    bool newObj = !trayIcon;
	    if (newObj) {
		if (!ns->toBoolean())
		    continue;
		trayIcon = new QSystemTrayIcon(wndWidget());
		trayIcon->setObjectName(QtClient::setUtf8(ns->name()));
		QtClient::connectObjects(trayIcon,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
		    this,SLOT(sysTrayIconAction(QSystemTrayIcon::ActivationReason)));
		// Deactivate program termination when the last window was hidden
		s_allHiddenQuit++;
	    }
	    ok = true;
	    // Add dynamic properties on creation
	    if (newObj)
		addDynamicProps(trayIcon,*nl);
	    // Set icon and tooltip
	    NamedString* tmp = nl->getParam("icon");
	    if (tmp && *tmp)
		trayIcon->setIcon(QIcon(QtClient::setUtf8(*tmp)));
	    tmp = nl->getParam("tooltip");
	    if (tmp && *tmp)
		trayIcon->setToolTip(QtClient::setUtf8(*tmp));
	    // Check context menu
	    NamedString* menu = nl->getParam("menu");
	    if (menu) {
		NamedList* nlMenu = static_cast<NamedList*>(menu->getObject("NamedList"));
		trayIcon->setContextMenu(nlMenu ? QtClient::buildMenu(*nlMenu,*menu,this,
		    SLOT(action()),SLOT(toggled(bool)),this) : 0);
	    }
	}
	setUpdatesEnabled(true);
	return ok;
    }
    // Parameters for the widget whose name is the list name
    if(params) {
	QtWidget w(wndWidget(), params);
	if (w.customTable()) {
	    bool ok = w.customTable()->setParams(params);
	    setUpdatesEnabled(true);
	    return ok;
	}
	if (w.type() == QtWidget::Calendar) {
	    int year = params.getIntValue("year");
	    int month = params.getIntValue("month");
	    int day = params.getIntValue("day");
	    w.calendar()->setCurrentPage(year, month);
	    w.calendar()->setSelectedDate(QDate(year, month, day));
	    setUpdatesEnabled(true);
	    return true;
	}
    }

    // Window or other parameters
    if (params.getBoolValue("modal")) {
	if (parentWindow())
	    setWindowModality(Qt::WindowModal);
	else
	    setWindowModality(Qt::ApplicationModal);
    }
    if (params.getBoolValue("minimized"))
	QWidget::setWindowState(Qt::WindowMinimized);
    bool ok = Window::setParams(params);
    setUpdatesEnabled(true);
    return ok;
}

void QtWindow::setOver(const Window* parent)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setOver(%p) [%p]",parent,this);
    QWidget::raise();
}

bool QtWindow::hasElement(const String& name)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::hasElement(%s) [%p]",name.c_str(),this);
    QtWidget w(wndWidget(),name);
    return w.valid();
}

bool QtWindow::setActive(const String& name, bool active)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setActive(%s,%s) [%p]",
	name.c_str(),String::boolText(active),this);
    bool ok = (name == m_id);
    if (ok) {
	if (QWidget::isMinimized())
	    QWidget::showNormal();
	QWidget::activateWindow();
    }
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return ok;
    if (w.type() != QtWidget::Action)
	w->setEnabled(active);
    else
	w.action()->setEnabled(active);
    return true;
}

bool QtWindow::setFocus(const String& name, bool select)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setFocus(%s,%s) [%p]",
	name.c_str(),String::boolText(select),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    w->setFocus();
    switch (w.type()) {
	case  QtWidget::ComboBox:
	    if (w.combo()->isEditable() && select)
		w.combo()->lineEdit()->selectAll();
	    break;
    }
    return true;
}

bool QtWindow::setShow(const String& name, bool visible)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setShow(%s,%s) [%p]",
	name.c_str(),String::boolText(visible),this);
    // Check system tray icons
    QSystemTrayIcon* trayIcon = findSysTrayIcon(this,name);
    if (trayIcon) {
	trayIcon->setVisible(visible);
	return true;
    }
    // Widgets
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    if (w.type() != QtWidget::Action)
	w->setVisible(visible);
    else
	w.action()->setVisible(visible);
    return true;
}

bool QtWindow::setText(const String& name, const String& text,
	bool richText)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setText(%s,%s) [%p]",
	m_id.c_str(),name.c_str(),text.c_str(),this);

    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::CheckBox:
	    w.check()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::LineEdit:
	    w.lineEdit()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::TextEdit:
	    if (richText) {
		w.textEdit()->clear();
		w.textEdit()->insertHtml(QtClient::setUtf8(text));
	    }
	    else
		w.textEdit()->setText(QtClient::setUtf8(text));
	    {
		QScrollBar* bar = w.textEdit()->verticalScrollBar();
		if (bar)
		    bar->setSliderPosition(bar->maximum());
	    }
	    return true;
	case QtWidget::Label:
	    w.label()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::ComboBox:
	    if (w.combo()->lineEdit())
		w.combo()->lineEdit()->setText(QtClient::setUtf8(text));
	    else
		setSelect(name,text);
	    return true;
	case QtWidget::Action:
	    w.action()->setText(QtClient::setUtf8(text));
	    return true;
	case QtWidget::SpinBox:
	    w.spinBox()->setValue(text.toInteger());
	    return true;
    }

    // Handle some known base classes having a setText() method
    if (w.inherits(QtWidget::AbstractButton))
	w.abstractButton()->setText(QtClient::setUtf8(text));
    else
	return false;
    return true;
}

bool QtWindow::setCheck(const String& name, bool checked)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setCheck(%s,%s) [%p]",
	name.c_str(),String::boolText(checked),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    if (w.inherits(QtWidget::AbstractButton))
	w.abstractButton()->setChecked(checked);
    else if (w.type() == QtWidget::Action)
	w.action()->setChecked(checked);
    else
	return false;
    return true;
}

bool QtWindow::setSelect(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setSelect(%s,%s) [%p]",
	name.c_str(),item.c_str(),this);

    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;

    int d = 0;
    switch (w.type()) {
	case QtWidget::CustomTable:
	    return w.customTable()->setSelect(item);
	case QtWidget::Table:
	    {
		TableWidget t(w);
		int row = t.getRow(item);
		if (row < 0)
		    return false;
		t.table()->setCurrentCell(row,0);
		return true;
	    }
	case QtWidget::ComboBox:
	    if (item) {
		d = w.findComboItem(item);
		if (d < 0)
		    return false;
	        w.combo()->setCurrentIndex(d);
	    }
	    else if (w.combo()->lineEdit())
		w.combo()->lineEdit()->setText("");
	    else
		return false;
	    return true;
	case QtWidget::ListBox:
	    d = w.findListItem(item);
	    if (d >= 0)
		w.list()->setCurrentRow(d);
	    return d >= 0;
	case QtWidget::Slider:
	    w.slider()->setValue(item.toInteger());
	    return true;
	case QtWidget::StackWidget:
	    d = item.toInteger(-1);
	    while (d < 0) {
		d = findStackedWidget(*(w.stackWidget()),item);
		if (d >= 0)
		    break;
		// Check for a default widget
		String def = YQT_OBJECT_NAME(w.stackWidget());
		def << "_default";
		d = findStackedWidget(*(w.stackWidget()),def);
		break;
	    }
	    if (d >= 0 && d < w.stackWidget()->count()) {
		w.stackWidget()->setCurrentIndex(d);
		return true;
	    }
	    return false;
	case QtWidget::ProgressBar:
	    d = item.toInteger();
	    if (d >= w.progressBar()->minimum() && d <= w.progressBar()->maximum())
		w.progressBar()->setValue(d);
	    else if (d < w.progressBar()->minimum())
		w.progressBar()->setValue(w.progressBar()->minimum());
	    else
		w.progressBar()->setValue(w.progressBar()->maximum());
	    return true;
	case QtWidget::Tab:
	    d = w.tab()->count() - 1;
	    for (QString tmp = QtClient::setUtf8(item); d >= 0; d--) {
		QWidget* wid = w.tab()->widget(d);
		if (wid && wid->objectName() == tmp)
		    break;
	    }
	    if (d >= 0 && d < w.tab()->count()) {
		w.tab()->setCurrentIndex(d);
		return true;
	    }
	    return false;

    }
    return false;
}

bool QtWindow::setUrgent(const String& name, bool urgent)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::setUrgent(%s,%s) [%p]",
	name.c_str(),String::boolText(urgent),this);

    if (name == m_id) {
	QApplication::alert(this,0);
	return true;
    }

    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    w->raise();
    return true;
}

bool QtWindow::hasOption(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::hasOption(%s,%s) [%p]",
	name.c_str(),item.c_str(),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::ComboBox:
	    return -1 != w.findComboItem(item);
	case QtWidget::Table:
	    return getTableRow(name,item);
	case QtWidget::ListBox:
	    return -1 != w.findListItem(item);
    }
    return false;
}

bool QtWindow::addOption(const String& name, const String& item, bool atStart,
	const String& text)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) addOption(%s,%s,%s,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),
	String::boolText(atStart),text.c_str(),this);

    QtWidget w(wndWidget(),name);
    switch (w.type()) {
	case QtWidget::ComboBox:
	    w.addComboItem(item,atStart);
	    if (atStart && w.combo()->lineEdit())
		w.combo()->lineEdit()->setText(w.combo()->itemText(0));
	    return true;
	case QtWidget::Table:
	    return addTableRow(name,item,0,atStart);
	case QtWidget::ListBox:
	    return w.addListItem(item,atStart);
    }
    return false;
}

bool QtWindow::delOption(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) delOption(%s,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),this);
    return delTableRow(name,item);
}

bool QtWindow::getOptions(const String& name, NamedList* items)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) getOptions(%s,%p) [%p]",
	m_id.c_str(),name.c_str(),items,this);

    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    if (!items)
	return true;

    switch (w.type()) {
	case QtWidget::ComboBox:
	    for (int i = 0; i < w.combo()->count(); i++)
		QtClient::getUtf8(*items,"",w.combo()->itemText(i),false);
	    break;
	case QtWidget::Table:
	    {
		TableWidget t(w.table(),false);
		for (int i = 0; i < t.rowCount(); i++) {
		    String item;
		    if (t.getCell(i,0,item) && item)
			items->addParam(item,"");
		}
	    }
	    break;
	case QtWidget::ListBox:
	    for (int i = 0; i < w.list()->count(); i++) {
		QListWidgetItem* tmp = w.list()->item(i);
		if (tmp)
		    QtClient::getUtf8(*items,"",tmp->text(),false);
	    }
	    break;
	case QtWidget::CustomTable:
	    return w.customTable()->getOptions(*items);
    }
    return true;
}

// Append or insert text lines to a widget
bool QtWindow::addLines(const String& name, const NamedList* lines, unsigned int max, 
	bool atStart)
{
    DDebug(ClientDriver::self(),DebugAll,"QtWindow(%s) addLines('%s',%p,%u,%s) [%p]",
	m_id.c_str(),name.c_str(),lines,max,String::boolText(atStart),this);

    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    if (!lines)
	return true;
    unsigned int count = lines->length();
    if (!count)
	return true;

    switch (w.type()) {
	case QtWidget::TextEdit:
	    // Limit the maximum number of paragraphs
	    if (max) {
		QTextDocument* doc = w.textEdit()->document();
		if (!doc)
		    return false;
		doc->setMaximumBlockCount((int)max);
	    }
	    {
		// FIXME: delete lines from begining if appending and the number
		//  of lines exceeds the maximum allowed
		QString s = w.textEdit()->toPlainText();
		int pos = atStart ? 0 : s.length();
		for (unsigned int i = 0; i < count; i++) {
		    NamedString* ns = lines->getParam(i);
		    if (!ns)
			continue;
		    if (ns->name().endsWith("\n"))
			s.insert(pos,QtClient::setUtf8(ns->name()));
		    else {
			String tmp = ns->name() + "\n";
			s.insert(pos,QtClient::setUtf8(tmp));
			pos++;
		    }
		    pos += (int)ns->name().length();
		}
		w.textEdit()->setText(s);
		// Scroll down if added at end
		if (!atStart) {
		    QScrollBar* bar = w.textEdit()->verticalScrollBar();
		    if (bar)
			bar->setSliderPosition(bar->maximum());
		}
	    }
	    return true;
	case QtWidget::Table:
	    // TODO: implement
	    break;
	case QtWidget::ComboBox:
	    if (atStart) {
		for (unsigned int i = count; i >= 0; i--) {
		    NamedString* ns = lines->getParam(i);
		    if (ns)
			w.combo()->insertItem(0,QtClient::setUtf8(ns->name()));
		}
		if (w.combo()->lineEdit())
		    w.combo()->lineEdit()->setText(w.combo()->itemText(0));
	    }
	    else { 
		for (unsigned int i = 0; i < count; i++) {
		    NamedString* ns = lines->getParam(i);
		    if (ns)
			w.combo()->addItem(QtClient::setUtf8(ns->name()));
		}
	    }
	    return true;
	case QtWidget::ListBox:
	    // TODO: implement
	    break;
    }
    return false;
}

bool QtWindow::addTableRow(const String& name, const String& item,
	const NamedList* data, bool atStart)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) addTableRow(%s,%s,%p,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),data,String::boolText(atStart),this);

    TableWidget tbl(wndWidget(),name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    if (custom)
	return custom->addTableRow(item,data,atStart);

    int row = atStart ? 0 : tbl.rowCount();
    tbl.addRow(row);
    // Set item (the first column) and the rest of data
    tbl.setID(row,item);
    if (data)
	tbl.updateRow(row,*data);
    return true;
}

// Insert or update multiple rows in a single operation
bool QtWindow::setMultipleRows(const String& name, const NamedList& data, const String& prefix)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setMultipleRows('%s',%p,'%s') [%p]",
	m_id.c_str(),name.c_str(),&data,prefix.c_str(),this);

    TableWidget tbl(wndWidget(),name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    return custom && custom->setMultipleRows(data,prefix);
}


// Insert a row into a table owned by this window
bool QtWindow::insertTableRow(const String& name, const String& item,
    const String& before, const NamedList* data)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) insertTableRow(%s,%s,%s,%p) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),before.c_str(),data,this);

    TableWidget tbl(wndWidget(),name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    if (custom)
	return custom->insertTableRow(item,before,data);

    int row = tbl.getRow(before);
    if (row == -1)
	row = tbl.rowCount();
    tbl.addRow(row);
    // Set item (the first column) and the rest of data
    tbl.setID(row,item);
    if (data)
	tbl.updateRow(row,*data);
    return true;
}

bool QtWindow::delTableRow(const String& name, const String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::delTableRow(%s,%s) [%p]",
	name.c_str(),item.c_str(),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    int row = -1;
    int n = 0;
    switch (w.type()) {
	case QtWidget::Table:
	case QtWidget::CustomTable:
	    {
		TableWidget tbl(w.table());
		QtTable* custom = tbl.customTable();
		if (custom) {
		    if (custom->delTableRow(item))
			row = 0;
		}
		else {
		    row = tbl.getRow(item);
		    if (row >= 0)
			tbl.delRow(row);
		}
		n = tbl.rowCount();
	    }
	    break;
	case QtWidget::ComboBox:
	    row = w.findComboItem(item);
	    if (row >= 0) {
		w.combo()->removeItem(row);
		n = w.combo()->count();
	    }
	    break;
	case QtWidget::ListBox:
	    row = w.findListItem(item);
	    if (row >= 0) {
		QStringListModel* model = (QStringListModel*)w.list()->model();
		if (!(model && model->removeRow(row)))
		    row = -1;
		n = w.list()->count();
	    }
	    break;
    }
    if (row < 0)
	return false;
    if (!n)
	raiseSelectIfEmpty(0,this,name);
    return true;
}

bool QtWindow::setTableRow(const String& name, const String& item, const NamedList* data)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) setTableRow(%s,%s,%p) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),data,this);

    TableWidget tbl(wndWidget(),name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    if (custom)
	return custom->setTableRow(item,data);

    int row = tbl.getRow(item);
    if (row < 0)
	return false;
    if (data)
	tbl.updateRow(row,*data);
    return true;
}

bool QtWindow::getTableRow(const String& name, const String& item, NamedList* data)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::getTableRow(%s,%s,%p) [%p]",
	name.c_str(),item.c_str(),data,this);

    TableWidget tbl(wndWidget(),name);
    if (!tbl.valid())
	return false;

    QtTable* custom = tbl.customTable();
    if (custom)
	return custom->getTableRow(item,data);

    int row = tbl.getRow(item);
    if (row < 0)
	return false;
    if (!data)
	return true;
    int n = tbl.columnCount();
    for (int i = 0; i < n; i++) {
	String name;
	if (!tbl.getHeaderText(i,name))
	    continue;
	String value;
	if (tbl.getCell(row,i,value))
	    data->setParam(name,value);
    }
    return true;
}

// Set a table row or add a new one if not found
bool QtWindow::updateTableRow(const String& name, const String& item,
    const NamedList* data, bool atStart)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) updateTableRow('%s','%s',%p,%s) [%p]",
	m_id.c_str(),name.c_str(),item.c_str(),data,String::boolText(atStart),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::Table:
	case QtWidget::CustomTable:
	    {
		TableWidget tbl(w.table());
		QtTable* custom = tbl.customTable();
		if (custom) {
		    if (custom->getTableRow(item))
			return custom->setTableRow(item,data);
		    return custom->addTableRow(item,data,atStart);
		}
		tbl.updateRow(item,data,atStart);
		return true;
	    }
	case QtWidget::ComboBox:
	    return w.findComboItem(item) >= 0 || w.addComboItem(item,atStart);
	case QtWidget::ListBox:
	    return w.findListItem(item) >= 0 || w.addListItem(item,atStart);
    }
    return false;
}

// Add or set one or more table row(s). Screen update is locked while changing the table.
// Each data list element is a NamedPointer carrying a NamedList with item parameters.
// The name of an element is the item to update.
// Element's value not empty: update the item
// Else: delete it
bool QtWindow::updateTableRows(const String& name, const NamedList* data, bool atStart)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) updateTableRows('%s',%p,%s) [%p]",
	m_id.c_str(),name.c_str(),data,String::boolText(atStart),this);

    TableWidget tbl(wndWidget(),name);
    if (!tbl.valid())
	return false;
    if (!data)
	return true;

    QtTable* custom = tbl.customTable();
    if (custom) {
	bool ok = custom->updateTableRows(data,atStart);
	raiseSelectIfEmpty(tbl.rowCount(),this,name);
	return ok;
    }

    bool ok = true;
    tbl.table()->setUpdatesEnabled(false);
    unsigned int n = data->length();
    for (unsigned int i = 0; i < n; i++) {
	if (Client::exiting())
	    break;

	// Get item and the list of parameters
	NamedString* ns = data->getParam(i);
	if (!ns)
	    continue;

	// Delete ?
	if (ns->null()) {
	    int row = tbl.getRow(ns->name());
	    if (row >= 0)
		tbl.delRow(row);
	    else
		ok = false;
	    continue;
	}

	NamedPointer* np = static_cast<NamedPointer*>(ns->getObject("NamedPointer"));
	NamedList* params = 0;
	if (np)
	    params = static_cast<NamedList*>(np->userObject("NamedList"));
	bool addNew = ns->toBoolean();

	if (addNew)
	    tbl.updateRow(ns->name(),params,atStart);
	else {
	    int row = tbl.getRow(ns->name());
	    bool found = (row >= 0);
	    if (found && params)
		tbl.updateRow(row,*params);
	    ok = found && ok;
	}
    }
    tbl.table()->setUpdatesEnabled(true);
    raiseSelectIfEmpty(tbl.rowCount(),this,name);
    return ok;
}

bool QtWindow::clearTable(const String& name)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::clearTable(%s) [%p]",name.c_str(),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    bool ok = true;
    if (w.widget())
	w->setUpdatesEnabled(false);
    switch (w.type()) {
	case QtWidget::Table:
	    while (w.table()->rowCount())
		w.table()->removeRow(0);
	    break;
	case QtWidget::TextEdit:
	    w.textEdit()->clear();
	    break;
	case QtWidget::ListBox:
	    w.list()->clear();
	    break;
	case QtWidget::ComboBox:
	    w.combo()->clear();
	    break;
	case QtWidget::CustomTable:
	    ok = w.customTable()->clearTable();
	    break;
	default:
	    ok = false;
    }
    if (w.widget())
	w->setUpdatesEnabled(true);
    return ok;
}

bool QtWindow::getText(const String& name, String& text, bool richText)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) getText(%s) [%p]",
	m_id.c_str(),name.c_str(),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::ComboBox:
	    QtClient::getUtf8(text,w.combo()->currentText());
	    return true;
	case QtWidget::LineEdit:
	    QtClient::getUtf8(text,w.lineEdit()->text());
	    return true;
	case QtWidget::TextEdit:
	    if (!richText)
		QtClient::getUtf8(text,w.textEdit()->toPlainText());
	    else
		QtClient::getUtf8(text,w.textEdit()->toHtml());
	    return true;
	case QtWidget::Label:
	    QtClient::getUtf8(text,w.label()->text());
	    return true;
	case QtWidget::Action:
	    QtClient::getUtf8(text,w.action()->text());
	    return true;
	case QtWidget::SpinBox:
	    text = w.spinBox()->value();
	    return true;
	default:
	    if (w.inherits(QtWidget::AbstractButton)) {
		QtClient::getUtf8(text,w.abstractButton()->text());
		return true;
	    }
     }
     return false;
}

bool QtWindow::getCheck(const String& name, bool& checked)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::getCheck(%s) [%p]",name.c_str(),this);

    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    if (w.inherits(QtWidget::AbstractButton))
	checked = w.abstractButton()->isChecked();
    else if (w.type() == QtWidget::Action)
	checked = w.action()->isChecked();
    else
	return false;
    return true;
}

bool QtWindow::getSelect(const String& name, String& item)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow::getSelect(%s) [%p]",name.c_str(),this);
    QtWidget w(wndWidget(),name);
    if (w.invalid())
	return false;
    switch (w.type()) {
	case QtWidget::ComboBox:
	    if (w.combo()->lineEdit() && w.combo()->lineEdit()->selectedText().isEmpty())
		return false;
	    QtClient::getUtf8(item,w.combo()->currentText());
	    return true;
	case QtWidget::Table:
	    {
		TableWidget t(w);
		int row = t.crtRow();
		return row >= 0 ? t.getCell(row,0,item) : false; 
	    }
	case QtWidget::ListBox:
	    {
		QListWidgetItem* crt = w.list()->currentItem();
		if (!crt)
		    return false;
		QtClient::getUtf8(item,crt->text());
	    }
	    return true;
	case QtWidget::Slider:
	    item = w.slider()->value();
	    return true;
	case QtWidget::ProgressBar:
	    item = w.progressBar()->value();
	    return true;
	case QtWidget::CustomTable:
	    return w.customTable()->getSelect(item);
	case QtWidget::Tab:
	    {
		item = "";
		QWidget* wid = w.tab()->currentWidget();
		if (wid)
		    QtClient::getUtf8(item,wid->objectName());
	    }
	    return true;
	case QtWidget::StackWidget:
	    {
		item = "";
		QWidget* wid = w.stackWidget()->currentWidget();
		if (wid)
		    QtClient::getUtf8(item,wid->objectName());
	    }
	    return true;
    }
    return false;
}

// Set a property for this window or for a widget owned by it
bool QtWindow::setProperty(const String& name, const String& item, const String& value)
{
    if (name == m_id)
	return QtClient::setProperty(wndWidget(),item,value);
    QObject* obj = qFindChild<QObject*>(wndWidget(),QtClient::setUtf8(name));
    return obj ? QtClient::setProperty(obj,item,value) : false;
}

// Get a property from this window or from a widget owned by it
bool QtWindow::getProperty(const String& name, const String& item, String& value)
{
    if (name == m_id)
	return QtClient::getProperty(wndWidget(),item,value);
    QObject* obj = qFindChild<QObject*>(wndWidget(),QtClient::setUtf8(name));
    return obj ? QtClient::getProperty(obj,item,value) : false;
}

bool QtWindow::event(QEvent* ev)
{
    if (ev->type() == QEvent::WindowDeactivate) {
	String hideProp;
	QtClient::getProperty(wndWidget(),s_propHideInactive,hideProp);
	if (hideProp && hideProp.toBoolean())
	    setVisible(false);
    }
    return QWidget::event(ev);
}

void QtWindow::closeEvent(QCloseEvent* event)
{
    // NOTE: Don't access window's data after calling hide():
    //  some logics might destroy the window when hidden

    // Notify window closed
    String tmp;
    if (Client::self() &&
	QtClient::getProperty(wndWidget(),"_yate_windowclosedaction",tmp))
	Client::self()->action(this,tmp);

    // Hide the window when requested
    String hideWnd;
    if (QtClient::getProperty(wndWidget(),"dynamicHideOnClose",hideWnd)	&&
	hideWnd.toBoolean()) {
	event->ignore();
	hide();
	return;
    }

    QWidget::closeEvent(event);
    if (m_mainWindow && Client::self()) {
	Client::self()->quit();
	return;
    }
    if (QtClient::getBoolProperty(wndWidget(),"_yate_destroyonclose")) {
	XDebug(QtDriver::self(),DebugAll,
	    "Window(%s) closeEvent() set delete later [%p]",m_id.c_str(),this);
	QObject::deleteLater();
	// Safe to call hide(): the window will be deleted when control returns
	//  to the main loop
    }
    hide();
}

void QtWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange)
	m_maximized = isMaximized();
    QWidget::changeEvent(event);
}

void QtWindow::action()
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) action() sender=%s [%p]",
	m_id.c_str(),YQT_OBJECT_NAME(sender()),this);
    if (!QtClient::self() || QtClient::changing())
	return;
    QtWidget w(sender());
    String name;
    NamedList* params = 0;
    if (translateName(w,name,&params))
	QtClient::self()->action(this,name,params);
    TelEngine::destruct(params);
}

// Toggled actions
void QtWindow::toggled(bool on)
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) toggled=%s sender=%s [%p]",
	m_id.c_str(),String::boolText(on),YQT_OBJECT_NAME(sender()),this);
    if (!QtClient::self() || QtClient::changing())
	return;
    QtWidget w(sender());
    String name;
    if (translateName(w,name))
	QtClient::self()->toggle(this,name,on);
}

// System tray actions
void QtWindow::sysTrayIconAction(QSystemTrayIcon::ActivationReason reason)
{
    String action;
    switch (reason) {
	case QSystemTrayIcon::Context:
	    QtClient::getProperty(sender(),s_propAction + "Context",action);
	    break;
	case QSystemTrayIcon::DoubleClick:
	    QtClient::getProperty(sender(),s_propAction + "DoubleClick",action);
	    break;
	case QSystemTrayIcon::Trigger:
	    QtClient::getProperty(sender(),s_propAction + "Trigger",action);
	    break;
	case QSystemTrayIcon::MiddleClick:
	    QtClient::getProperty(sender(),s_propAction + "MiddleClick",action);
	    break;
	default:
	    return;
    }
    if (action)
	Client::self()->action(this,action);
}

// Choose file window was accepted
void QtWindow::chooseFileAccepted()
{
    QFileDialog* dlg = qobject_cast<QFileDialog*>(sender());
    if (!dlg)
	return;
    String action;
    QtClient::getUtf8(action,dlg->objectName());
    if (!action)
	return;
    NamedList params("");
    QDir dir = dlg->directory();
    if (dir.absolutePath().length())
	QtClient::getUtf8(params,"dir",fixPathSep(dir.absolutePath()));
    QStringList files = dlg->selectedFiles();
    for (int i = 0; i < files.size(); i++)
	QtClient::getUtf8(params,"file",fixPathSep(files[i]));
    if (dlg->fileMode() != QFileDialog::DirectoryOnly &&
	dlg->fileMode() != QFileDialog::Directory) {
	QString filter = dlg->selectedFilter();
	if (filter.length())
	    QtClient::getUtf8(params,"filter",filter);
    }
    Client::self()->action(this,action,&params);
}

// Choose file window was cancelled
void QtWindow::chooseFileRejected()
{
    QFileDialog* dlg = qobject_cast<QFileDialog*>(sender());
    if (!dlg)
	return;
    String action;
    QtClient::getUtf8(action,dlg->objectName());
    if (!action)
	return;
    Client::self()->action(this,action,0);
}

// Text changed slot. Notify the client
void QtWindow::textChanged(const QString& text)
{
    if (!sender())
	return;
    NamedList params("");
    QtClient::getUtf8(params,"sender",sender()->objectName());
    QtClient::getUtf8(params,"text",text);
    Client::self()->action(this,"textchanged",&params);
}

void QtWindow::openUrl(const QString& link)
{
    QDesktopServices::openUrl(QUrl(link));
}

void QtWindow::doubleClick()
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) doubleClick() sender=%s [%p]",
	m_id.c_str(),YQT_OBJECT_NAME(sender()),this);
    if (QtClient::self() && sender())
	Client::self()->action(this,YQT_OBJECT_NAME(sender()));
}

// A widget's selection changed
void QtWindow::selectionChanged()
{
    XDebug(QtDriver::self(),DebugAll,"QtWindow(%s) selectionChanged() sender=%s [%p]",
	m_id.c_str(),YQT_OBJECT_NAME(sender()),this);
    if (!(QtClient::self() && sender()))
	return;
    String name = YQT_OBJECT_NAME(sender());
    QtWidget w(sender());
    if (w.type() != QtWidget::Calendar) {
	String item;
	getSelect(name,item);
	Client::self()->select(this,name,item);
    }
    else {
	NamedList p("");
	QDate d = w.calendar()->selectedDate();
	p.addParam("year",String(d.year()));
	p.addParam("month",String(d.month()));
	p.addParam("day",String(d.day()));
	Client::self()->action(this,name,&p);
    }
}

// Load a widget from file
QWidget* QtWindow::loadUI(const char* fileName, QWidget* parent,
	const char* uiName, const char* path)
{
    if (Client::exiting())
	return 0;
    if (!(fileName && *fileName && parent))
	return 0;

    if (!(path && *path))
	path = Client::s_skinPath.c_str();
    UIBuffer* buf = UIBuffer::build(fileName);
    const char* err = 0;
    if (buf && buf->buffer()) {
	QBuffer b(buf->buffer());
	QUiLoader loader;
        loader.setWorkingDirectory(QDir(QtClient::setUtf8(path)));
	QWidget* w = loader.load(&b,parent);
	if (w)
	    return w;
	err = "loader failed";
    }
    else
	err = buf ? "file is empty" : "file not found";
    // Error
    TelEngine::destruct(buf);
    Debug(DebugWarn,"Failed to load widget '%s' file='%s' path='%s': %s",
        uiName,fileName,path,err);
    return 0;
}

// Clear the UI cache
void QtWindow::clearUICache(const char* fileName)
{
    if (!fileName)
	UIBuffer::s_uiCache.clear();
    else
	TelEngine::destruct(UIBuffer::s_uiCache.find(fileName));
}

// Filter events
bool QtWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (!obj)
	return false;
    // Apply dynamic properties changes
    if (event->type() == QEvent::DynamicPropertyChange) {
	String name = YQT_OBJECT_NAME(obj);
	QDynamicPropertyChangeEvent* ev = static_cast<QDynamicPropertyChangeEvent*>(event);
	String prop = ev->propertyName().constData();
	// Handle only yate dynamic properties
	if (!prop.startsWith(s_yatePropPrefix,false))
	    return QWidget::eventFilter(obj,event);
	XDebug(QtDriver::self(),DebugAll,"Window(%s) eventFilter(%s) prop=%s [%p]",
	    m_id.c_str(),YQT_OBJECT_NAME(obj),prop.c_str(),this);
	// Return false for now on: it's our property
	QtWidget w(obj);
	if (w.invalid())
	    return false;
	String value;
	if (!QtClient::getProperty(obj,prop,value))
	    return false;
	bool ok = true;
	bool handled = true;
	if (prop == s_propColWidths) {
	    if (w.table()) {
		ObjList* list = value.split(',',false);
		unsigned int col = 0;
		for (ObjList* o = list->skipNull(); o; o = o->skipNext(), col++) {
		    int width = (static_cast<String*>(o->get()))->toInteger(-1);
		    if (width >= 0)
			w.table()->setColumnWidth(col,width);
		}
		TelEngine::destruct(list);
	    }
	}
	else if (prop == s_propWindowFlags) {
	    QWidget* wid = (name == m_id || name == m_oldId) ? this : w.widget();
	    // Set window flags from enclosed widget:
	    //  custom window title/border/sysmenu config
	    ObjList* f = value.split(',',false);
	    wid->setWindowFlags(Qt::CustomizeWindowHint);
	    int flags = wid->windowFlags();
	    // Clear settable flags
	    TokenDict* dict = s_windowFlags;
	    for (int i = 0; dict[i].token; i++)
		flags &= ~dict[i].value;
	    // Set flags
	    for (ObjList* o = f->skipNull(); o; o = o->skipNext())
		flags |= lookup(o->get()->toString(),s_windowFlags,0);
	    TelEngine::destruct(f);
	    wid->setWindowFlags((Qt::WindowFlags)flags);
	}
	else if (prop == s_propHHeader) {
	    // Show/hide the horizontal header
	    ok = ((w.type() == QtWidget::Table || w.type() == QtWidget::CustomTable) &&
		value.isBoolean() && w.table()->horizontalHeader());
	    if (ok)
		w.table()->horizontalHeader()->setVisible(value.toBoolean());
	}
	else
	    ok = handled = false;
	if (ok)
	    DDebug(ClientDriver::self(),DebugAll,
		"Applied dynamic property %s='%s' for object='%s'",
		prop.c_str(),value.c_str(),name.c_str());
	else if (handled)
	    Debug(ClientDriver::self(),DebugMild,
		"Failed to apply dynamic property %s='%s' for object='%s'",
		prop.c_str(),value.c_str(),name.c_str());
	return false;
    }
    if (event->type() == QEvent::KeyPress) {
	static int mask = Qt::SHIFT | Qt::CTRL | Qt::ALT;

	if (!Client::self())
	    return QWidget::eventFilter(obj,event);
	QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
	QWidget* wid = QApplication::focusWidget();
	if (!wid)
	    return false;
	// Check if we should raise an action for the widget
	QKeySequence ks(keyEvent->key());
	String prop;
	QtClient::getUtf8(prop,ks.toString());
	prop = s_propAction + prop;
	String action;
	getProperty(YQT_OBJECT_NAME(wid),prop,action);
	if (!action)
	    return QWidget::eventFilter(obj,event);
	QVariant v = wid->property(prop + "Modifiers");
	// Get modifiers from property and check them against event
	int tmp = 0;
	if (v.type() == QVariant::String) {
	    QKeySequence ks(v.toString());
	    for (unsigned int i = 0; i < ks.count(); i++)
		tmp |= ks[i];
	}
	if (tmp == (mask & keyEvent->modifiers())) {
	    // Check if we should let the control process the key
	    QVariant v = wid->property(prop + "Filter");
	    bool ret = ((v.type() == QVariant::Bool) ? v.toBool() : false);
	    QObject* obj = qFindChild<QObject*>(this,QtClient::setUtf8(action));
	    bool trigger = true;
	    if (obj) {
		QtWidget w(wndWidget(),action);
		if (w.widget())
		    trigger = w.widget()->isEnabled();
		else if (w.type() == QtWidget::Action)
		    trigger = w.action()->isEnabled();
	    }
	    if (trigger)
		Client::self()->action(this,action);
	    return ret;
	}
    }

    return QWidget::eventFilter(obj,event);
}

// Handle key pressed events
void QtWindow::keyPressEvent(QKeyEvent* event)
{
    if (!Client::self())
	return QWidget::keyPressEvent(event);
    if (event->key() == Qt::Key_Backspace)
	Client::self()->backspace(m_id,this);
    return QWidget::keyPressEvent(event);
}

// Show hide window. Notify the client
void QtWindow::setVisible(bool visible)
{
    if (visible && !isMaximized()) {
	// Override position for notification windows
	if (QtClient::getBoolProperty(wndWidget(),"_yate_notificationwindow")) {
	    QDesktopWidget* d = QApplication::desktop();
	    if (d) {
		QRect r = d->availableGeometry(this);
		if (r.width() > m_width)
		    m_x = r.width() - m_width;
		if (r.height() > m_height)
		    m_y = r.height() - m_height;
	    }
	}
	QWidget::move(m_x,m_y);
	resize(m_width,m_height);
    }
    QWidget::setVisible(visible);
    // Notify the client on window visibility changes
    bool changed = (m_visible != visible);
    m_visible = visible;
    if (changed && Client::self()) {
	QVariant var;
	if (wndWidget())
	    var = wndWidget()->property("dynamicUiActionVisibleChanged");
	if (!var.toBool())
	    Client::self()->toggle(this,"window_visible_changed",m_visible);
	else {
	    Message* m = new Message("ui.action");
	    m->addParam("action","window_visible_changed");
	    m->addParam("visible",String::boolText(m_visible));
	    m->addParam("window",m_id);
	    Engine::enqueue(m);
	}
    }
    if (!m_visible && QtClient::getBoolProperty(wndWidget(),"_yate_destroyonhide")) {
	XDebug(QtDriver::self(),DebugAll,
	    "Window(%s) setVisible(false) set delete later [%p]",m_id.c_str(),this);
	QObject::deleteLater();
    }
    // Destroy owned dialogs
    if (!m_visible) {
	QList<QDialog*> d = qFindChildren<QDialog*>(this);
	for (int i = 0; i < d.size(); i++)
	    delete d[i];
    }
}

// Show the window
void QtWindow::show()
{
    setVisible(true);
    m_maximized = m_maximized || isMaximized();
    if (m_maximized)
	setWindowState(Qt::WindowMaximized);
}

// Hide the window
void QtWindow::hide()
{
    setVisible(false);
}

void QtWindow::size(int width, int height)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::size(%d,%d) [%p]",width,height,this);
}

void QtWindow::move(int x, int y)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::move(%d,%d) [%p]",x,y,this);
    m_x = x;
    m_y = y;
    QWidget::move(x, y);
}

void QtWindow::moveRel(int dx, int dy)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::moveRel(%d,%d) [%p]",dx,dy,this);
}

bool QtWindow::related(const Window* wnd) const
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::related(%p) [%p]",wnd,this);
    return false;
}

void QtWindow::menu(int x, int y)
{
    DDebug(QtDriver::self(),DebugAll,"QtWindow::menu(%d,%d) [%p]",x,y,this);
}

// Create a modal dialog
bool QtWindow::createDialog(const String& name, const String& title, const String& alias,
    const NamedList* params)
{
    QtDialog* d = new QtDialog(wndWidget());
    if (d->show(name,title,alias,params))
	return true;
    delete d;
    return false;
}

// Destroy a modal dialog
bool QtWindow::closeDialog(const String& name)
{
    QDialog* d = qFindChild<QDialog*>(this,QtClient::setUtf8(name));
    if (!d)
	return false;
    delete d;
    return true;
}

// Load UI file and setup the window
void QtWindow::doPopulate()
{
    Debug(QtDriver::self(),DebugAll,"Populating window '%s' [%p]",m_id.c_str(),this);
    QWidget* formWidget = loadUI(m_description,this,m_id);
    if (!formWidget)
	return;
    QSize frame = frameSize();
    setMinimumSize(formWidget->minimumSize().width(),formWidget->minimumSize().height());
    setMaximumSize(formWidget->maximumSize().width(),formWidget->maximumSize().height());
    resize(formWidget->width(),formWidget->height());
    QtClient::setWidget(this,formWidget);
    m_widget = YQT_OBJECT_NAME(formWidget);
    String wTitle;
    QtClient::getUtf8(wTitle,formWidget->windowTitle());
    title(wTitle);
    setWindowIcon(formWidget->windowIcon());
    setStyleSheet(formWidget->styleSheet());
}

// Initialize window
void QtWindow::doInit()
{
    DDebug(QtDriver::self(),DebugAll,"Initializing window '%s' [%p]",
	m_id.c_str(),this);

    // Create window's dynamic properties from config
    Configuration cfg(Engine::configFile(m_oldId),false);
    NamedList* sectGeneral = cfg.getSection("general");
    if (sectGeneral)
	addDynamicProps(wndWidget(),*sectGeneral);

    // Load window data
    m_mainWindow = s_cfg.getBoolValue(m_oldId,"mainwindow");
    m_saveOnClose = s_cfg.getBoolValue(m_oldId,"save",true);
    NamedList* sect = s_save.getSection(m_id);
    if (sect) {
	m_maximized = sect->getBoolValue("maximized");
	m_x = sect->getIntValue("x",pos().x());
	m_y = sect->getIntValue("y",pos().y());
	m_width = sect->getIntValue("width",width());
	m_height = sect->getIntValue("height",height());
	m_visible = sect->getBoolValue("visible");
    }
    else {
	Debug(QtDriver::self(),DebugNote,"Window(%s) not found in config [%p]",
	    m_id.c_str(),this);
	m_visible = s_cfg.getBoolValue(m_oldId,"visible");
    }
    m_visible = m_mainWindow || m_visible;
    if (!m_width)
	m_width = wndWidget()->width();
    if (!m_height)
	m_height = wndWidget()->height();

    // Build custom UI widgets from frames owned by this widget
    QtClient::buildFrameUiWidgets(this);

    // Create custom widgets from
    // _yate_identity=customwidget|[separator=sep|] sep widgetclass sep widgetname [sep param=value]
    QList<QFrame*> frm = qFindChildren<QFrame*>(wndWidget());
    for (int i = 0; i < frm.size(); i++) {
	String create;
	QtClient::getProperty(frm[i],"_yate_identity",create);
	if (!create.startSkip("customwidget|",false))
	    continue;
	char sep = '|';
	// Check if we have another separator
	if (create.startSkip("separator=",false)) {
	    if (create.length() < 2)
		continue;
	    sep = create.at(0);
	    create = create.substr(2);
	}
	ObjList* list = create.split(sep,false);
	String type;
	String name;
	NamedList params("");
	int what = 0;
	for (ObjList* o = list->skipNull(); o; o = o->skipNext(), what++) {
	    GenObject* p = o->get();
	    if (what == 0)
		type = p->toString();
	    else if (what == 1)
		name = p->toString();
	    else {
		// Decode param
		int pos = p->toString().find('=');
		if (pos != -1)
		    params.addParam(p->toString().substr(0,pos),p->toString().substr(pos + 1));
	    }
	}
	TelEngine::destruct(list);
	params.addParam("parentwindow",m_id);
	NamedString* pw = new NamedString("parentwidget");
	QtClient::getUtf8(*pw,frm[i]->objectName());
	params.addParam(pw);
	QObject* obj = (QObject*)UIFactory::build(type,name,&params);
	if (!obj)
	    continue;
	QWidget* wid = qobject_cast<QWidget*>(obj);
	if (wid)
	    QtClient::setWidget(frm[i],wid);
	else {
	    obj->setParent(frm[i]);
	    QtCustomObject* customObj = qobject_cast<QtCustomObject*>(obj);
	    if (customObj)
		customObj->parentChanged();
	}
    }

    // Create window's children dynamic properties from config
    unsigned int n = cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* sect = cfg.getSection(i);
	if (sect && *sect && *sect != "general")
	    addDynamicProps(qFindChild<QObject*>(this,sect->c_str()),*sect);
    }

    // Process "_yate_setaction" property for our children
    QtClient::setAction(this);

    // Connect actions' signal
    QList<QAction*> actions = qFindChildren<QAction*>(wndWidget());
    for (int i = 0; i < actions.size(); i++) {
	String addToWidget;
	QtClient::getProperty(actions[i],"dynamicAddToParent",addToWidget);
	if (addToWidget && addToWidget.toBoolean())
	    QWidget::addAction(actions[i]);
	if (actions[i]->isCheckable())
	    QtClient::connectObjects(actions[i],SIGNAL(toggled(bool)),this,SLOT(toggled(bool)));
	else
	    QtClient::connectObjects(actions[i],SIGNAL(triggered()),this,SLOT(action()));
    }

    // Connect combo boxes signals
    QList<QComboBox*> combos = qFindChildren<QComboBox*>(wndWidget());
    for (int i = 0; i < combos.size(); i++) {
	QtClient::connectObjects(combos[i],SIGNAL(activated(int)),this,SLOT(selectionChanged()));
	if (QtClient::getBoolProperty(combos[i],"_yate_textchangednotify"))
	    QtClient::connectObjects(combos[i],SIGNAL(editTextChanged(const QString&)),this,
				     SLOT(textChanged(const QString&)));
    }

    // Connect abstract buttons (check boxes and radio/push/tool buttons) signals
    QList<QAbstractButton*> buttons = qFindChildren<QAbstractButton*>(wndWidget());
    for(int i = 0; i < buttons.size(); i++)
	if (QtClient::autoConnect(buttons[i]))
	    connectButton(buttons[i]);

    // Connect group boxes signals
    QList<QGroupBox*> grp = qFindChildren<QGroupBox*>(wndWidget());
    for(int i = 0; i < grp.size(); i++)
	if (grp[i]->isCheckable())
	    QtClient::connectObjects(grp[i],SIGNAL(toggled(bool)),this,SLOT(toggled(bool)));

    // Connect sliders signals
    QList<QSlider*> sliders = qFindChildren<QSlider*>(wndWidget());
    for (int i = 0; i < sliders.size(); i++)
	QtClient::connectObjects(sliders[i],SIGNAL(valueChanged(int)),this,SLOT(selectionChanged()));

    // Connect calendar widget signals
    QList<QCalendarWidget*> cals = qFindChildren<QCalendarWidget*>(wndWidget());
    for (int i = 0; i < cals.size(); i++)
	QtClient::connectObjects(cals[i],SIGNAL(selectionChanged()),this,SLOT(selectionChanged()));

    // Connect list boxes signals
    QList<QListWidget*> lists = qFindChildren<QListWidget*>(wndWidget());
    for (int i = 0; i < lists.size(); i++) {
	QtClient::connectObjects(lists[i],SIGNAL(itemDoubleClicked(QListWidgetItem*)),
	    this,SLOT(doubleClick()));
	QtClient::connectObjects(lists[i],SIGNAL(itemActivated(QListWidgetItem*)),
	    this,SLOT(doubleClick()));
	QtClient::connectObjects(lists[i],SIGNAL(currentRowChanged(int)),
	    this,SLOT(selectionChanged()));
    }

    // Connect tab widget signals
    QList<QTabWidget*> tabs = qFindChildren<QTabWidget*>(wndWidget());
    for (int i = 0; i < tabs.size(); i++)
	QtClient::connectObjects(tabs[i],SIGNAL(currentChanged(int)),this,SLOT(selectionChanged()));

    // Connect stacked widget signals
    QList<QStackedWidget*> sw = qFindChildren<QStackedWidget*>(wndWidget());
    for (int i = 0; i < sw.size(); i++)
	QtClient::connectObjects(sw[i],SIGNAL(currentChanged(int)),this,SLOT(selectionChanged()));

    // Connect line edit signals
    QList<QLineEdit*> le = qFindChildren<QLineEdit*>(wndWidget());
    for (int i = 0; i < le.size(); i++) {
	if (QtClient::getBoolProperty(le[i],"_yate_textchangednotify"))
	    QtClient::connectObjects(le[i],SIGNAL(textChanged(const QString&)),this,
		SLOT(textChanged(const QString&)));
    }

    // Process tables:
    // Insert a column and connect signals
    // Hide columns starting with "hidden:"
    QList<QTableWidget*> tables = qFindChildren<QTableWidget*>(wndWidget());
    for (int i = 0; i < tables.size(); i++) {
	bool nonCustom = (0 == qobject_cast<QtTable*>(tables[i]));
	// Horizontal header
	QHeaderView* hdr = tables[i]->horizontalHeader();
	// Stretch last column
	bool b = QtClient::getBoolProperty(tables[i],"_yate_horizontalstretch",true);
	hdr->setStretchLastSection(b);
	if (!QtClient::getBoolProperty(tables[i],"_yate_horizontalheader",true))
	    hdr->hide();
	// Vertical header
	hdr = tables[i]->verticalHeader();
	int itemH = QtClient::getIntProperty(tables[i],"_yate_rowheight");
	if (itemH > 0)
	    hdr->setDefaultSectionSize(itemH);
	if (!QtClient::getBoolProperty(tables[i],"_yate_verticalheader"))
	    hdr->hide();
	else {
	    int width = QtClient::getIntProperty(tables[i],"_yate_verticalheaderwidth");
	    if (width > 0)
		hdr->setFixedWidth(width);
	    if (!QtClient::getBoolProperty(tables[i],"_yate_allowvheaderresize"))
		hdr->setResizeMode(QHeaderView::Fixed);
	}
	if (nonCustom) {
	    // Set _yate_save_props
	    QVariant var = tables[i]->property(s_propsSave);
	    if (var.type() != QVariant::StringList) {
		// Create the property if not found, ignore it if not a string list
		if (var.type() == QVariant::Invalid)
		    var = QVariant(QVariant::StringList);
		else
		    Debug(QtDriver::self(),DebugNote,
			"Window(%s) table '%s' already has a non string list property %s [%p]",
			m_id.c_str(),YQT_OBJECT_NAME(tables[i]),s_propsSave.c_str(),this);
	    }
	    if (var.type() == QVariant::StringList) {
		// Make sure saved properties exists to allow them to be restored
		QStringList sl = var.toStringList();
		bool changed = createProperty(tables[i],s_propColWidths,QVariant::String,this,&sl);
		if (changed)
		    tables[i]->setProperty(s_propsSave,QVariant(sl));
	    }
	}
	TableWidget t(tables[i]);
	// Insert the column containing the ID
	t.addColumn(0,0,"hidden:id");
	// Hide columns
	for (int i = 0; i < t.columnCount(); i++) {
	    String name;
	    t.getHeaderText(i,name,false);
	    if (name.startsWith("hidden:"))
		t.table()->setColumnHidden(i,true);
	}
	// Connect signals
	QtClient::connectObjects(t.table(),SIGNAL(cellDoubleClicked(int,int)),
	    this,SLOT(doubleClick()));
#if 0
	// This would generate action() twice since QT will signal both cell and
	// table item double click
	QtClient::connectObjects(t.table(),SIGNAL(itemDoubleClicked(QTableWidgetItem*)),
	    this,SLOT(doubleClick()));
#endif
	String noSel;
	getProperty(t.name(),"dynamicNoItemSelChanged",noSel);
	if (!noSel.toBoolean())
	    QtClient::connectObjects(t.table(),SIGNAL(itemSelectionChanged()),
		this,SLOT(selectionChanged()));
	// Optionally connect cell clicked
	// This is done when we want to generate a select() or action() from cell clicked
	String cellClicked;
	getProperty(t.name(),"dynamicCellClicked",cellClicked);
	if (cellClicked) {
	    if (cellClicked == "selectionChanged")
		QtClient::connectObjects(t.table(),SIGNAL(cellClicked(int,int)),
		    this,SLOT(selectionChanged()));
	    else if (cellClicked == "doubleClick")
		QtClient::connectObjects(t.table(),SIGNAL(cellClicked(int,int)),
		    this,SLOT(doubleClick()));
	}
    }

    // Restore saved children properties
    if (sect) {
	unsigned int n = sect->length();
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* ns = sect->getParam(i);
	    if (!ns)
		continue;
	    String prop(ns->name());
	    if (!prop.startSkip("property:",false))
		continue;
	    int pos = prop.find(":");
	    if (pos > 0) {
		String wName = prop.substr(0,pos);
		String pName = prop.substr(pos + 1);
		DDebug(QtDriver::self(),DebugAll,
		    "Window(%s) restoring property %s=%s for child '%s' [%p]",
		    m_id.c_str(),pName.c_str(),ns->c_str(),wName.c_str(),this);
		setProperty(wName,pName,*ns);
	    }
	}
    }

    // Install event filter and apply dynamic properties
    QList<QObject*> w = qFindChildren<QObject*>(wndWidget());
    for (int i = 0; i < w.size(); i++) {
	QList<QByteArray> props = w[i]->dynamicPropertyNames();
	// Check for our dynamic properties
	int j = 0;
	for (j = 0; j < props.size(); j++)
	    if (props[j].startsWith(s_yatePropPrefix))
		break;
	if (j == props.size())
	    continue;
	// Add event hook to be used when a dynamic property changes
	w[i]->installEventFilter(this);
	// Fake dynamic property change to apply them
	for (j = 0; j < props.size(); j++) {
	    if (!props[j].startsWith(s_yatePropPrefix))
		continue;
	    QDynamicPropertyChangeEvent ev(props[j]);
	    eventFilter(w[i],&ev);
	}
    }

    qRegisterMetaType<QModelIndex>("QModelIndex");
    qRegisterMetaType<QTextCursor>("QTextCursor");

    // Force window visibility change notification by changing the visibility flag
    // Some controls might need to be updated
    m_visible = !m_visible;
    if (m_visible)
	hide();
    else
	show();
}

// Mouse button pressed notification
void QtWindow::mousePressEvent(QMouseEvent* event)
{
    if (Qt::LeftButton == event->button() && !isMaximized()) {
	m_movePos = event->globalPos();
	m_moving = true;
    }
}

// Mouse button release notification
void QtWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (Qt::LeftButton == event->button())
	m_moving = false;
}

// Move the window if the moving flag is set
void QtWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_moving || Qt::LeftButton != event->buttons() || isMaximized())
	return;
    int cx = event->globalPos().x() - m_movePos.x();
    int cy = event->globalPos().y() - m_movePos.y();
    if (cx || cy) {
	m_movePos = event->globalPos();
	QWidget::move(x() + cx,y() + cy);
    }
}

// Update window position and size
void QtWindow::updatePosSize()
{
    QPoint point = pos();
    m_x = point.x();
    m_y = point.y();
    m_width = width();
    m_height = height();
}


/*
 * QtDialog
 */
// Destructor. Notify the client if not exiting
QtDialog::~QtDialog()
{
    QtWindow* w = parentWindow();
    if (w && m_notifyOnClose && Client::valid())
	QtClient::self()->action(w,buildActionName(m_notifyOnClose,m_notifyOnClose));
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s) QtDialog(%s) destroyed [%p]",
	w ? w->id().c_str() : "",YQT_OBJECT_NAME(this),w);
}

// Initialize dialog. Load the widget.
// Connect non checkable actions to own slot.
// Connect checkable actions/buttons to parent window's slot
// Display the dialog on success
bool QtDialog::show(const String& name, const String& title, const String& alias,
    const NamedList* params)
{
    QtWindow* w = parentWindow();
    if (!w)
	return false;
    QWidget* widget = QtWindow::loadUI(Client::s_skinPath + s_cfg.getValue(name,"description"),this,name);
    if (!widget)
	return false;
    QtClient::getProperty(widget,"_yate_notifyonclose",m_notifyOnClose);
    setObjectName(QtClient::setUtf8(alias ? alias : name));
    setMinimumSize(widget->minimumSize().width(),widget->minimumSize().height());
    setMaximumSize(widget->maximumSize().width(),widget->maximumSize().height());
    resize(widget->width(),widget->height());
    QtClient::setWidget(this,widget);
    if (title)
	setWindowTitle(QtClient::setUtf8(title));
    else if (widget->windowTitle().length())
	setWindowTitle(widget->windowTitle());
    else
	setWindowTitle(w->windowTitle());
    // Connect abstract buttons (check boxes and radio/push/tool buttons) signals
    QList<QAbstractButton*> buttons = qFindChildren<QAbstractButton*>(widget);
    for(int i = 0; i < buttons.size(); i++) {
	if (!QtClient::autoConnect(buttons[i]))
	    continue;
	if (!buttons[i]->isCheckable())
	    QtClient::connectObjects(buttons[i],SIGNAL(clicked()),this,SLOT(action()));
	else
	    QtClient::connectObjects(buttons[i],SIGNAL(toggled(bool)),w,SLOT(toggled(bool)));
    }
    // Connect actions' signal
    QList<QAction*> actions = qFindChildren<QAction*>(widget);
    for (int i = 0; i < actions.size(); i++) {
	if (!QtClient::autoConnect(actions[i]))
	    continue;
	if (!actions[i]->isCheckable())
	    QtClient::connectObjects(actions[i],SIGNAL(triggered()),this,SLOT(action()));
	else
	    QtClient::connectObjects(actions[i],SIGNAL(toggled(bool)),w,SLOT(toggled(bool)));
    }
    if (params)
	w->setParams(*params);
    setWindowModality(Qt::WindowModal);
    QDialog::show();
    return true;
}

// Notify client
void QtDialog::action()
{
    QtWindow* w = parentWindow();
    if (!w)
	return;
    DDebug(QtDriver::self(),DebugAll,"QtWindow(%s) dialog action '%s' [%p]",
	w->id().c_str(),YQT_OBJECT_NAME(sender()),w);
    if (!QtClient::self() || QtClient::changing())
	return;
    String name;
    QtClient::getIdentity(sender(),name);
    if (name && QtClient::self()->action(w,buildActionName(name,name)))
	deleteLater();
}

// Delete the dialog
void QtDialog::closeEvent(QCloseEvent* event)
{
    QDialog::closeEvent(event);
    deleteLater();
}


/**
 * QtClient
 */
QtClient::QtClient()
    : Client("Qt Client")
{
    m_oneThread = Engine::config().getBoolValue("client","onethread",true);

    s_save = Engine::configFile("qt4client",true);
    s_save.load();
}

QtClient::~QtClient()
{
}

void QtClient::cleanup()
{
    Client::cleanup();
    m_events.clear();
    Client::save(s_save);
    QtWindow::clearUICache();
    m_app->quit();
    delete m_app;
}

void QtClient::run()
{
    const char* style = Engine::config().getValue("client","style");
    if (style && !QApplication::setStyle(QString::fromUtf8(style)))
	Debug(ClientDriver::self(),DebugWarn,"Could not set Qt style '%s'",style);
    int argc = 0;
    char* argv =  0;
    m_app = new QApplication(argc,&argv);
    m_app->setQuitOnLastWindowClosed(false);
    String imgRead;
    QList<QByteArray> imgs = QImageReader::supportedImageFormats();
    for (int i = 0; i < imgs.size(); i++)
	imgRead.append(imgs[i].constData(),",");
    imgRead = "read image formats '" + imgRead + "'";
    Debug(ClientDriver::self(),DebugInfo,"QT client start running (version=%s) %s",
	qVersion(),imgRead.c_str());
    if (!QSound::isAvailable())
	Debug(ClientDriver::self(),DebugWarn,"QT sounds are not available");
    // Create events proxy
    m_events.append(new QtEventProxy(QtEventProxy::Timer));
    m_events.append(new QtEventProxy(QtEventProxy::AllHidden,m_app));
    Client::run();
}

void QtClient::main()
{
    m_app->exec();
}

void QtClient::lock()
{}

void QtClient::unlock()
{}

void QtClient::allHidden()
{
    Debug(QtDriver::self(),DebugInfo,"QtClient::allHiden() counter=%d",s_allHiddenQuit);
    if (s_allHiddenQuit > 0)
	return;
    quit();
}

bool QtClient::createWindow(const String& name, const String& alias)
{
    String parent = s_cfg.getValue(name,"parent");
    QtWindow* parentWnd = 0;
    if (!TelEngine::null(parent)) {
	ObjList* o = m_windows.find(parent);
	if (o)
	    parentWnd = YOBJECT(QtWindow,o->get());
    }
    QtWindow* w = new QtWindow(name,s_skinPath + s_cfg.getValue(name,"description"),alias,parentWnd);
    if (w) {
	Debug(QtDriver::self(),DebugAll,"Created window name=%s alias=%s with parent=(%s [%p]) (%p)",
	    name.c_str(),alias.c_str(),parent.c_str(),parentWnd,w);
	// Remove the old window
	ObjList* o = m_windows.find(w->id());
	if (o)
	    Client::self()->closeWindow(w->id(),false);
	w->populate();
	m_windows.append(w);
	return true;
    }
    else
	Debug(QtDriver::self(),DebugGoOn,"Could not create window name=%s alias=%s",
	    name.c_str(),alias.c_str());
    return false;
}

void QtClient::loadWindows(const char* file)
{
    if (!file)
	s_cfg = s_skinPath + "qt4client.rc";
    else
	s_cfg = String(file);
    s_cfg.load();
    Debug(QtDriver::self(),DebugInfo,"Loading Windows");
    unsigned int n = s_cfg.sections();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* l = s_cfg.getSection(i);
	if (l && l->getBoolValue("enabled",true))
	    createWindow(*l);
    }
}

// Open a file open dialog window
// Parameters that can be specified include 'caption',
//  'dir', 'filter', 'selectedfilter', 'confirmoverwrite', 'choosedir'
bool QtClient::chooseFile(Window* parent, NamedList& params)
{
    QtWindow* wnd = static_cast<QtWindow*>(parent);
    // Don't set the dialog's parent: window's style sheet will be propagated to
    //  child dialog and we might have incomplete (not full) custom styled controls
    QFileDialog* dlg = new QFileDialog(0,setUtf8(params.getValue("caption")),
	setUtf8(params.getValue("dir")));

    if (wnd)
	dlg->setWindowIcon(wnd->windowIcon());

    // Connect signals
    String* action = params.getParam("action");
    if (wnd && !null(action)) {
	dlg->setObjectName(setUtf8(*action));
	QtClient::connectObjects(dlg,SIGNAL(accepted()),wnd,SLOT(chooseFileAccepted()));
	QtClient::connectObjects(dlg,SIGNAL(rejected()),wnd,SLOT(chooseFileRejected()));
    }

    // Destroy it when closed
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // This dialog should always stay on top
    dlg->setWindowFlags(dlg->windowFlags() | Qt::WindowStaysOnTopHint);

    // Window modality doesn't work without a parent so make it application modal
    if (params.getBoolValue("modal",true))
	dlg->setWindowModality(Qt::ApplicationModal);

    // Filters
    NamedString* f = params.getParam("filters");
    if (f) {
	QStringList filters;
	ObjList* obj = f->split('|',false);
	for (ObjList* o = obj->skipNull(); o; o = o->skipNext())
	    filters.append(QtClient::setUtf8(o->get()->toString()));
	TelEngine::destruct(obj);
	dlg->setFilters(filters);
    }
    QString flt = QtClient::setUtf8(params.getValue("selectedfilter"));
    if (flt.length())
	dlg->selectFilter(flt);

    if (params.getBoolValue("save"))
	dlg->setAcceptMode(QFileDialog::AcceptSave);
    else
	dlg->setAcceptMode(QFileDialog::AcceptOpen);

    // Choose options
    if (params.getBoolValue("choosefile",true)) {
	if (params.getBoolValue("chooseanyfile"))
	    dlg->setFileMode(QFileDialog::AnyFile);
	else if (params.getBoolValue("multiplefiles"))
	    dlg->setFileMode(QFileDialog::ExistingFiles);
	else
	    dlg->setFileMode(QFileDialog::ExistingFile);
    }
    else
	dlg->setFileMode(QFileDialog::DirectoryOnly);
 
    dlg->selectFile(QtClient::setUtf8(params.getValue("selectedfile")));

    dlg->setVisible(true);
    return true;
}

bool QtClient::action(Window* wnd, const String& name, NamedList* params)
{
    String tmp = name;
    if (tmp.startSkip("openurl:",false))
	return openUrl(tmp);
    return Client::action(wnd,name,params);
}

// Create a sound object. Append it to the global list
bool QtClient::createSound(const char* name, const char* file, const char* device)
{
    if (!(name && *name && file && *file))
	return false;
    Lock lock(ClientSound::s_soundsMutex);
    if (ClientSound::s_sounds.find(name))
	return false;
    ClientSound::s_sounds.append(new QtSound(name,file,device));
    DDebug(ClientDriver::self(),DebugAll,"Added sound=%s file=%s device=%s",
	name,file,device);
    return true;
}

// Build a date/time string from UTC time
bool QtClient::formatDateTime(String& dest, unsigned int secs,
    const char* format, bool utc)
{
    if (!(format && *format))
	return false;
    QtClient::getUtf8(dest,formatDateTime(secs,format,utc));
    return true;
}

// Build a date/time QT string from UTC time
QString QtClient::formatDateTime(unsigned int secs, const char* format, bool utc)
{
    QDateTime time;
    if (utc)
	time.setTimeSpec(Qt::UTC);
    time.setTime_t(secs);
    return time.toString(format);
}

// Retrieve an object's QtWindow parent
QtWindow* QtClient::parentWindow(QObject* obj)
{
    for (; obj; obj = obj->parent()) {
	QtWindow* w = qobject_cast<QtWindow*>(obj);
	if (w)
	    return w;
    }
    return 0;
}

// Save an object's property into parent window's section. Clear it on failure
bool QtClient::saveProperty(QObject* obj, const String& prop, QtWindow* owner)
{
    if (!obj)
	return false;
    if (!owner)
	owner = parentWindow(obj);
    if (!owner)
	return false;
    String value;
    bool ok = getProperty(obj,prop,value);
    String pName;
    pName << "property:" << YQT_OBJECT_NAME(obj) << ":" << prop;
    if (ok)
	s_save.setValue(owner->id(),pName,value);
    else
	s_save.clearKey(owner->id(),pName);
    return ok;
}

// Set or an object's property
bool QtClient::setProperty(QObject* obj, const char* name, const String& value)
{
    if (!(obj && name && *name))
	return false;
    QVariant var = obj->property(name);
    const char* err = 0;
    bool ok = false;
    switch (var.type()) {
	case QVariant::String:
	    ok = obj->setProperty(name,QVariant(QtClient::setUtf8(value)));
	    break;
	case QVariant::Bool:
	    ok = obj->setProperty(name,QVariant(value.toBoolean()));
	    break;
	case QVariant::Int:
	    ok = obj->setProperty(name,QVariant(value.toInteger()));
	    break;
	case QVariant::UInt:
	    ok = obj->setProperty(name,QVariant((unsigned int)value.toInteger()));
	    break;
	case QVariant::Icon:
	    ok = obj->setProperty(name,QVariant(QIcon(QtClient::setUtf8(value))));
	    break;
	case QVariant::Pixmap:
	    ok = obj->setProperty(name,QVariant(QPixmap(QtClient::setUtf8(value))));
	    break;
	case QVariant::Double:
	    ok = obj->setProperty(name,QVariant(value.toDouble()));
	    break;
	case QVariant::KeySequence:
	    ok = obj->setProperty(name,QVariant(QtClient::setUtf8(value)));
	    break;
	case QVariant::Invalid:
	    err = "no such property";
	    break;
	default:
	    err = "unsupported type";
    }
    if (ok)
	DDebug(ClientDriver::self(),DebugAll,"Set property %s=%s for object '%s'",
	    name,value.c_str(),YQT_OBJECT_NAME(obj));
    else
	DDebug(ClientDriver::self(),DebugNote,
	    "Failed to set %s=%s (type=%s) for object '%s': %s",
	    name,value.c_str(),var.typeName(),YQT_OBJECT_NAME(obj),err);
    return ok;
}

// Get an object's property
bool QtClient::getProperty(QObject* obj, const char* name, String& value)
{
    if (!(obj && name && *name))
	return false;
    QVariant var = obj->property(name);
    if (var.type() == QVariant::StringList) {
	NamedList* l = static_cast<NamedList*>(value.getObject("NamedList"));
	if (l)
	    copyParams(*l,var.toStringList());
	else
	    getUtf8(value,var.toStringList().join(","));
	DDebug(ClientDriver::self(),DebugAll,"Got list property %s for object '%s'",
	    name,YQT_OBJECT_NAME(obj));
	return true;
    }
    if (var.canConvert(QVariant::String)) {
	QtClient::getUtf8(value,var.toString());
	DDebug(ClientDriver::self(),DebugAll,"Got property %s=%s for object '%s'",
	    name,value.c_str(),YQT_OBJECT_NAME(obj));
	return true;
    }
    DDebug(ClientDriver::self(),DebugNote,
	"Failed to get property '%s' (type=%s) for object '%s': %s",
	name,var.typeName(),YQT_OBJECT_NAME(obj),
	((var.type() == QVariant::Invalid) ? "no such property" : "unsupported type"));
    return false;
}

// Copy a string list to a list of parameters
void QtClient::copyParams(NamedList& dest, const QStringList& src)
{
    for (int i = 0; i < src.size(); i++) {
	if (!src[i].length())
	    continue;
	int pos = src[i].indexOf('=');
	String name;
	if (pos >= 0) {
	    getUtf8(name,src[i].left(pos));
	    getUtf8(dest,name,src[i].right(src[i].length() - pos - 1));
	}
	else {
	    getUtf8(name,src[i]);
	    dest.addParam(name,"");
	}
    }
}

// Copy a list of parameters to string list
void QtClient::copyParams(QStringList& dest, const NamedList& src)
{
    unsigned int n = src.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = src.getParam(i);
	if (ns)
	    dest.append(setUtf8(ns->name() + "=" + *ns));
    }
}

// Build custom UI widgets from frames owned by a widget
void QtClient::buildFrameUiWidgets(QWidget* parent)
{
    if (!parent)
	return;
    QList<QFrame*> frm = qFindChildren<QFrame*>(parent);
    for (int i = 0; i < frm.size(); i++) {
	if (!getBoolProperty(frm[i],"_yate_uiwidget"))
	    continue;
	String name;
	String type;
	getProperty(frm[i],"_yate_uiwidget_name",name);
	getProperty(frm[i],"_yate_uiwidget_class",type);
	if (!(name && type))
	    continue;
	NamedList params("");
	getProperty(frm[i],"_yate_uiwidget_params",params);
	QtWindow* w = static_cast<QtWindow*>(parent->window());
	if (w)
	    params.setParam("parentwindow",w->id());
	getUtf8(params,"parentwidget",frm[i]->objectName(),true);
	QObject* obj = (QObject*)UIFactory::build(type,name,&params);
	if (!obj)
	    continue;
	QWidget* wid = qobject_cast<QWidget*>(obj);
	if (wid)
	    QtClient::setWidget(frm[i],wid);
	else {
	    obj->setParent(frm[i]);
	    QtCustomObject* customObj = qobject_cast<QtCustomObject*>(obj);
	    if (customObj)
		customObj->parentChanged();
	}
    }
}

// Associate actions to buttons with '_yate_setaction' property set
void QtClient::setAction(QWidget* parent)
{
    if (!parent)
	return;
    QList<QToolButton*> tb = qFindChildren<QToolButton*>(parent);
    for (int i = 0; i < tb.size(); i++) {
	QVariant var = tb[i]->property("_yate_setaction");
	if (var.toString().isEmpty())
	    continue;
	QAction* a = qFindChild<QAction*>(parent,var.toString());
	if (a)
	    tb[i]->setDefaultAction(a);
    }
}

// Build a menu object from a list of parameters
QMenu* QtClient::buildMenu(NamedList& params, const char* text, QObject* receiver,
	 const char* triggerSlot, const char* toggleSlot, QWidget* parent,
	 const char* aboutToShowSlot)
{
    QMenu* menu = 0;
    unsigned int n = params.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = params.getParam(i);
	if (!(param && param->name().startsWith("item:")))
	    continue;

	if (!menu)
	    menu = new QMenu(setUtf8(text),parent);

	NamedList* p = static_cast<NamedList*>(param->getObject("NamedList"));
	if (p)  {
	    QMenu* subMenu = buildMenu(*p,*param,receiver,triggerSlot,toggleSlot,menu);
	    if (subMenu)
		menu->addMenu(subMenu);
	    continue;
	}
	String name = param->name().substr(5);
	if (*param) {
	    QAction* a = menu->addAction(QtClient::setUtf8(*param));
	    a->setObjectName(QtClient::setUtf8(name));
	    a->setParent(menu);
	}
	else if (!name)
	    menu->addSeparator()->setParent(menu);
	else {
	    // Check if the action is already there
	    QAction* a = 0;
	    if (parent && parent->window())
		a = qFindChild<QAction*>(parent->window(),QtClient::setUtf8(name));
	    if (a)
		menu->addAction(a);
	    else
		Debug(ClientDriver::self(),DebugNote,
		    "buildMenu(%s) action '%s' not found",params.c_str(),name.c_str());
	}
    }

    if (!menu)
	return 0;

    // Set name
    menu->setObjectName(setUtf8(params));
    // Apply properties
    // Format: property:object_name:property_name=value
    if (parent)
	for (unsigned int i = 0; i < n; i++) {
	    NamedString* param = params.getParam(i);
	    if (!(param && param->name().startsWith("property:")))
		continue;
	    int pos = param->name().find(':',9);
	    if (pos < 9)
		continue;
	    QObject* obj = qFindChild<QObject*>(parent,setUtf8(param->name().substr(9,pos - 9)));
	    if (obj)
		setProperty(obj,param->name().substr(pos + 1),*param);
	}
    // Connect signals (direct children only: actions from sub-menus are already connected)
    QList<QAction*> list = qFindChildren<QAction*>(menu);
    for (int i = 0; i < list.size(); i++) {
	if (list[i]->isSeparator() || list[i]->parent() != menu)
	    continue;
	if (list[i]->isCheckable())
	    QtClient::connectObjects(list[i],SIGNAL(toggled(bool)),receiver,toggleSlot);
	else
	    QtClient::connectObjects(list[i],SIGNAL(triggered()),receiver,triggerSlot);
    }
    if (!TelEngine::null(aboutToShowSlot))
	QtClient::connectObjects(menu,SIGNAL(aboutToShow()),receiver,aboutToShowSlot);

    return menu;
}

// Wrapper for QObject::connect() used to put a debug mesage on failure
bool QtClient::connectObjects(QObject* sender, const char* signal,
    QObject* receiver, const char* slot)
{
    if (!(sender && signal && *signal && receiver && slot && *slot))
	return false;
    bool ok = QObject::connect(sender,signal,receiver,slot);
    if (ok)
	DDebug(QtDriver::self(),DebugAll,
	    "Connected sender=%s signal=%s to receiver=%s slot=%s",
	    YQT_OBJECT_NAME(sender),signal,YQT_OBJECT_NAME(receiver),slot);
    else
	Debug(QtDriver::self(),DebugWarn,
	    "Failed to connect sender=%s signal=%s to receiver=%s slot=%s",
	    YQT_OBJECT_NAME(sender),signal,YQT_OBJECT_NAME(receiver),slot);
    return ok;
}

// Insert a widget into another one replacing any existing children
bool QtClient::setWidget(QWidget* parent, QWidget* child)
{
    if (!(parent && child))
	return false;
    QVBoxLayout* layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(child);
    QLayout* l = parent->layout();
    if (l)
	delete l;
    parent->setLayout(layout);
    return true;
}


/**
 * QtDriver
 */
QtDriver::QtDriver()
    : m_init(false)
{
    qInstallMsgHandler(qtMsgHandler);
}

QtDriver::~QtDriver()
{
    qInstallMsgHandler(0);
}

void QtDriver::initialize()
{
    Output("Initializing module Qt4 client");
    s_device = Engine::config().getValue("client","device",DEFAULT_DEVICE);
    if (!QtClient::self()) {
	debugCopy();
	new QtClient;
	QtClient::self()->startup();
    }
    if (!m_init) {
	m_init = true;
	setup();
    }
}

/**
 * QtEventProxy
 */
QtEventProxy::QtEventProxy(Type type, QApplication* app)
{
#define SET_NAME(n) { m_name = n; setObjectName(QtClient::setUtf8(m_name)); }
    switch (type) {
	case Timer:
	    SET_NAME("qtClientTimerProxy");
	    {
		QTimer* timer = new QTimer(this);
		timer->setObjectName("qtClientIdleTimer");
		QtClient::connectObjects(timer,SIGNAL(timeout()),this,SLOT(timerTick()));
		timer->start(0);
	    }
	    break;
	case AllHidden:
	    SET_NAME("qtClientAllHidden");
	    if (app)
		QtClient::connectObjects(app,SIGNAL(lastWindowClosed()),this,SLOT(allHidden()));
	    break;
	default:
	    return;
    }
#undef SET_NAME
}

void QtEventProxy::timerTick()
{
    if (Client::self())
	Client::self()->idleActions();
    Thread::idle();
}

void QtEventProxy::allHidden()
{
    if (Client::self())
	Client::self()->allHidden();
}

/**
 * QtSound
 */
bool QtSound::doStart()
{
    doStop();
    if (Client::self())
	Client::self()->createObject((void**)&m_sound,"QSound",m_file);
    if (m_sound)
	DDebug(ClientDriver::self(),DebugAll,"Sound(%s) started file=%s",
	    c_str(),m_file.c_str());
    else
	Debug(ClientDriver::self(),DebugNote,"Sound(%s) failed to start file=%s",
	    c_str(),m_file.c_str());
    m_sound->setLoops(m_repeat ? m_repeat : -1);
    m_sound->play();
    return true;
}

void QtSound::doStop()
{
    if (!m_sound)
	return;
    m_sound->stop();
    delete m_sound;
    DDebug(ClientDriver::self(),DebugAll,"Sound(%s) stopped",c_str());
    m_sound = 0;
}

/* vi: set ts=8 sw=4 sts=4 noet: */
