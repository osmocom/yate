/**
 * jabberserver.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Jabber Server module
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

// TODO:
// - Fix stream termination on shutdown
// - Notify 'offline' on closing server streams
// - Remove offline messages from database when succesfully sent (not when enqueued in the stream)


#include <yatephone.h>
#include <yatejabber.h>
#include <stdlib.h>

using namespace TelEngine;

namespace { // anonymous

class YStreamReceive;                    // Stream receive thread
class YStreamSetReceive;                 // A list of stream receive threads
class YStreamProcess;                    // Stream process (getEvent()) thread
class YStreamSetProcess;                 // A list of stream process threads
class YJBConnectThread;                  // Stream connect thread
class YJBEntityCapsList;                 // Entity capbilities
class YJBEngine;                         // Jabber engine
class JBPendingJob;                      // A pending stanza waiting to be routed/processed
class JBPendingWorker;                   // A thread processing pending jobs containing stanzas with
                                         // the same from/to hash
class UserAuthMessage;                   // 'user.auth' message enqueued when a stream requires user password
class JBMessageHandler;                  // Module message handlers
class TcpListener;                       // Incoming connection listener
class JBModule;                          // The module


/*
 * Stream receive thread
 */
class YStreamReceive : public JBStreamSetReceive, public Thread
{
public:
    inline YStreamReceive(JBStreamSetList* owner, Thread::Priority prio = Thread::Normal)
	: JBStreamSetReceive(owner), Thread("JBStreamReceive",prio)
	{}
    virtual bool start()
	{ return Thread::startup(); }
    virtual void stop()
	{ Thread::cancel(); }
protected:
    virtual void run()
	{ JBStreamSetReceive::run(); }
};

/*
 * A list of stream receive threads
 */
class YStreamSetReceive : public JBStreamSetList
{
public:
    inline YStreamSetReceive(JBEngine* engine, unsigned int max, const char* name)
	: JBStreamSetList(engine,max,0,name)
	{}
protected:
    virtual JBStreamSet* build()
	{ return new YStreamReceive(this); }
};

/*
 * Stream process (getEvent()) thread
 */
class YStreamProcess : public JBStreamSetProcessor, public Thread
{
public:
    inline YStreamProcess(JBStreamSetList* owner, Thread::Priority prio = Thread::Normal)
	: JBStreamSetProcessor(owner), Thread("JBStreamProcess",prio)
	{}
    virtual bool start()
	{ return Thread::startup(); }
    virtual void stop()
	{ Thread::cancel(); }
protected:
    virtual void run()
	{ JBStreamSetProcessor::run(); }
};

/*
 * A list of stream process threads
 */
class YStreamSetProcess : public JBStreamSetList
{
public:
    inline YStreamSetProcess(JBEngine* engine, unsigned int max, const char* name)
	: JBStreamSetList(engine,max,0,name)
	{}
protected:
    virtual JBStreamSet* build()
	{ return new YStreamProcess(this); }
};

/*
 * Stream connect thread
 */
class YJBConnectThread : public JBConnect, public Thread
{
public:
    inline YJBConnectThread(const JBStream& stream)
	: JBConnect(stream), Thread("YJBConnectThread")
	{}
    virtual void stopConnect()
	{ cancel(false); }
    virtual void run()
	{ JBConnect::connect(); }
};

/*
 * Entity capability
 */
class YJBEntityCapsList : public JBEntityCapsList
{
public:
    // Load the entity caps file
    void load();
protected:
    inline void getEntityCapsFile(String& file) {
	    file = Engine::configPath();
	    if (!file.endsWith(Engine::pathSeparator()))
		file << Engine::pathSeparator();
	    file << "jabberentitycaps.xml";
	}
    // Notify changes and save the entity caps file
    virtual void capsAdded(JBEntityCaps* caps);
};

/*
 * Jabber engine
 */
class YJBEngine : public JBServerEngine
{
public:
    YJBEngine();
    ~YJBEngine();
    // (Re)initialize the engine
    void initialize(const NamedList* params, bool first = false);
    // Process events
    virtual void processEvent(JBEvent* ev);
    // Build an internal stream name from node name and stream index
    virtual void buildStreamName(String& name, const JBStream* stream);
    // Start stream TLS
    virtual void encryptStream(JBStream* stream);
    // Connect an outgoing stream
    virtual void connectStream(JBStream* stream);
    // Build a dialback key
    virtual void buildDialbackKey(const String& id, const String& local,
	const String& remote, String& key);
    // Check if a domain is serviced by this engine
    virtual bool hasDomain(const String& domain);
    // Get the first domain in the list
    inline void firstDomain(String& domain) {
	    Lock lock(this);
	    ObjList* o = m_domains.skipNull();
	    if (o)
		domain = o->get()->toString();
	}
    // Retrieve a subdomain of a serviced domain
    void getSubDomain(String& subdomain, const String& domain);
    // Add or remove a component to/from serviced domains and components list
    void setComponent(const String& domain, bool add);
    // Check if a component is serviced by this engine
    bool hasComponent(const String& domain);
    // Check if a resource name is retricted
    bool restrictedResource(const String& name);
    // Check if a domain is serviced by a server item
    bool isServerItemDomain(const String& domain);
    // Internally route c2s <--> comp stanzas
    // Return true if handled
    bool routeInternal(JBEvent* ev);
    // Process 'user.roster' notification messages
    void handleUserRoster(Message& msg);
    // Process 'user.update' messages
    void handleUserUpdate(Message& msg);
    // Process 'jabber.iq' messages
    bool handleJabberIq(Message& msg);
    // Process 'resource.subscribe' messages
    bool handleResSubscribe(Message& msg);
    // Process 'resource.notify' messages
    bool handleResNotify(Message& msg);
    // Process 'msg.execute' messages
    bool handleMsgExecute(Message& msg);
    // Process 'jabber.item' messages
    bool handleJabberItem(Message& msg);
    // Handle 'presence' stanzas
    // s2s: Destination domain was already checked by the lower layer
    // The given event is always valid and carry a valid stream and xml element
    void processPresenceStanza(JBEvent* ev);
    // Process a stream start element received by an incoming stream
    // The given event is always valid and carry a valid stream
    // Set local domain and stream features to advertise to remote party
    void processStartIn(JBEvent* ev);
    // Process Auth events from incoming streams
    // The given event is always valid and carry a valid stream
    void processAuthIn(JBEvent* ev);
    // Process Bind events
    // The given event is always valid and carry a valid stream
    void processBind(JBEvent* ev);
    // Process stream Running, Destroy, Terminated events
    // The given event is always valid and carry a valid stream
    void processStreamEvent(JBEvent* ev);
    // Process stream DbResult events
    // The given event is always valid and carry a valid stream
    void processDbResult(JBEvent* ev);
    // Process stream DbVerify events
    // The given event is always valid and carry a valid stream
    void processDbVerify(JBEvent* ev);
    // Process all incoming jabber:iq:roster
    // The given event is always valid and carry a valid element
    // Return the response
    XmlElement* processIqRoster(JBEvent* ev, JBStream::Type sType, XMPPUtils::IqType t);
    // Process all incoming vcard-temp with target in our domain(s)
    // The given event is always valid and carry a valid element
    // Return the response
    XmlElement* processIqVCard(JBEvent* ev, JBStream::Type sType, XMPPUtils::IqType t);
    // Process all incoming jabber:iq:private
    // The given event is always valid and carry a valid element
    // Return the response
    XmlElement* processIqPrivate(JBEvent* ev, JBStream::Type sType, XMPPUtils::IqType t);
    // Process all incoming jabber:iq:register stanzas
    // The given event is always valid and carry a valid element
    XmlElement* processIqRegister(JBEvent* ev, JBStream::Type sType, XMPPUtils::IqType t,
	const String& domain, int flags);
    // Process all incoming jabber:iq:auth stanzas
    // The given event is always valid and carry a valid element
    XmlElement* processIqAuth(JBEvent* ev, JBStream::Type sType, XMPPUtils::IqType t, int flags);
    // Handle disco info requests addressed to the server
    XmlElement* discoInfo(JBEvent* ev, JBStream::Type sType);
    // Handle disco items requests addressed to the server
    XmlElement* discoItems(JBEvent* ev);
    // Send an XML element to list of client streams.
    // The given pointers will be consumed and zeroed
    // Return true if the element was succesfully sent on at least one stream
    bool sendStanza(XmlElement*& xml, ObjList*& streams);
    // Find a server stream used to send stanzas from local domain to remote
    // Build a new one if not found
    JBStream* getServerStream(const JabberID& from, const JabberID& to);
    // Notify online/offline presence from client streams
    void notifyPresence(JBClientStream& cs, bool online, XmlElement* xml,
	const String& capsId);
    // Notify directed online/offline presence
    void notifyPresence(const JabberID& from, const JabberID& to, bool online,
	XmlElement* xml, bool fromRemote, bool toRemote, const String& capsId);
    // Build a jabber.feature message
    Message* jabberFeature(XmlElement* xml, XMPPNamespace::Type t, JBStream::Type sType,
	const char* from, const char* to = 0, const char* operation = 0);
    // Build a xmpp.iq message
    Message* xmppIq(JBEvent* ev, const char* service);
    // Build an user.(un)register message
    Message* userRegister(JBStream& stream, bool reg, const char* instance = 0);
    // Fill module status
    void statusParams(String& str);
    unsigned int statusDetail(String& str, JBStream::Type t = JBStream::TypeCount,
	JabberID* remote = 0);
    void statusDetail(String& str, const String& name);
    // Complete stream detail
    void streamDetail(String& str, JBStream* stream);
    // Complete remote party jid starting with partWord
    void completeStreamRemote(String& str, const String& partWord, JBStream::Type t);
    // Complete stream name starting with partWord
    void completeStreamName(String& str, const String& partWord);
    // Remove a resource from binding resources list
    inline void removeBindingResource(const JabberID& user) {
	    Lock lock(this);
	    ObjList* o = user ? findBindingRes(user) : 0;
	    if (o)
		o->remove();
	}
    // Program name and version to be advertised on request
    String m_progName;
    String m_progVersion;
private:
    // Notify an incoming s2s stream about a dialback verify response
    void notifyDbVerifyResult(const JabberID& local, const JabberID& remote,
	const String& id, XMPPError::Type rsp);
    // Find a configured or dynamic domain
    inline ObjList* findDomain(const String& domain, bool cfg) {
	    ObjList* o = cfg ? m_domains.skipNull() : m_dynamicDomains.skipNull();
	    for (; o; o = o->skipNext())
		if (*static_cast<String*>(o->get()) == domain)
		    return o;
	    return 0;
	}
    // Add a resource to binding resources list. Make sure the resource is unique
    // Return true on success
    bool bindingResource(const JabberID& user);
    inline ObjList* findBindingRes(const JabberID& user) {
	    for (ObjList* o = m_bindingResources.skipNull(); o; o = o->skipNext())
		if (user == *static_cast<JabberID*>(o->get()))
		    return o;
	    return 0;
	}

    bool m_c2sTlsRequired;               // TLS is required on c2s streams
    bool m_allowUnsecurePlainAuth;       // Allow user plain password auth on unsecured streams
    ObjList m_domains;                   // Domains serviced by this engine
    ObjList m_dynamicDomains;            // Dynamic domains (components or items)
    ObjList m_restrictedResources;       // Resource names the users can't use
    ObjList m_items;
    ObjList m_components;
    XMPPFeatureList m_c2sFeatures;       // Server features to advertise on c2s streams
    XMPPFeatureList m_features;          // Server features to advertise on non c2s streams
    String m_dialbackSecret;             // Server dialback secret used to build keys
    ObjList m_bindingResources;          // List of resources in bind process
};

/*
 * A pending stanza waiting to be routed/processed
 * It is used to serialize stanzas sent by an user to another one
 */
class JBPendingJob : public GenObject
{
public:
    JBPendingJob(JBEvent* ev);
    inline ~JBPendingJob()
	{ TelEngine::destruct(m_event); }
    // Retrieve the stream from jabber engine
    JBStream* getStream();
    // Retrieve the stream from jabber engine. Send the given stanza
    // Set regular=false to use JBStream::sendStreamXml()
    // The pointer will be consumed and zeroed
    void sendStanza(XmlElement*& xml, bool regular = true);
    // Build and send an iq result stanza
    inline void sendIqResultStanza(XmlElement* child = 0) {
	    XmlElement* xml = m_event->buildIqResult(false,child);
	    sendStanza(xml);
	}
    // Build and send an iq error stanza
    inline void sendIqErrorStanza(XMPPError::Type error,
	XMPPError::ErrorType type = XMPPError::TypeModify) {
	    XmlElement* xml = m_event->buildIqError(false,error,0,type);
	    sendStanza(xml);
	}
    // Build and send a message error stanza
    inline void sendChatErrorStanza(XMPPError::Type error,
	XMPPError::ErrorType type = XMPPError::TypeModify) {
	    XmlElement* xml = XMPPUtils::createMessage("error",0,0,m_event->id(),0);
	    xml->addChild(XMPPUtils::createError(type,error));
	    sendStanza(xml);
	}

    // Release memory
    virtual void destruct() {
	TelEngine::destruct(m_event);
	GenObject::destruct();
    }

    JBEvent* m_event;
    String m_stream;                     // The id of the stream receiving the stanza
    JBStream::Type m_streamType;         // The type of the stream
    String m_local;                      // Stream local domain
    int m_flags;                         // Stream flags
    bool m_serverTarget;                 // The recipient is the server itself
    bool m_serverItemTarget;             // The recipient is a server item
};

/*
 * A thread processing pending jobs
 * s2s streams: The hash is built from the 'from and 'to' attributes
 * Otherwise: The hash is built from the 'from' attribute
 */
class JBPendingWorker : public Thread, public Mutex
{
public:
    JBPendingWorker(unsigned int index, Thread::Priority prio);
    virtual void cleanup();
    virtual void run();

    // Initialize (start) the worker threads
    static void initialize(unsigned int threads, Thread::Priority prio = Thread::Normal);
    // Cancel worker threads. Wait for them to terminate
    static void stop();
    // Add a job to one of the threads
    static bool add(JBEvent* ev);

    static JBPendingWorker** s_threads;  // Worker threads
    static unsigned int s_threadCount;   // Worker threads count
    static Mutex s_mutex;                // Mutex used to protect the public global data

private:
    // Process chat jobs
    void processChat(JBPendingJob& job);
    // Process iq jobs
    void processIq(JBPendingJob& job);
    // Reset the global index
    bool resetIndex();

    ObjList m_jobs;                      // The list is currently processing the first job
    unsigned int m_index;                // Thread index in global list
};

/*
 * 'user.auth' message enqueued when a stream requires user password
 */
class UserAuthMessage : public Message
{
public:
    // Fill the message with authentication data
    UserAuthMessage(JBEvent* ev);
    ~UserAuthMessage();
    JabberID m_bindingUser;
protected:
    // Check accepted and returned value. Calls stream's authenticated() method
    virtual void dispatched(bool accepted);

    String m_stream;
    JBStream::Type m_streamType;
};

/*
 * Module message handlers
 */
class JBMessageHandler : public MessageHandler
{
public:
    // Message handlers
    // Non-negative enum values will be used as handler priority
    enum {
	ResSubscribe = -1,             // YJBEngine::handleResSubscribe()
	ResNotify    = -2,             // YJBEngine::handleResNotify()
	UserRoster   = -3,             // YJBEngine::handleUserRoster()
	UserUpdate   = -4,             // YJBEngine::handleUserUpdate()
	JabberItem   = -5,             // YJBEngine::handleJabberItem()
	JabberIq     = 150,            // YJBEngine::handleJabberIq()
    };
    JBMessageHandler(int handler);
protected:
    virtual bool received(Message& msg);
private:
    int m_handler;
};

/*
 * Incoming connection listener
 */
class TcpListener : public Thread, public String
{
public:
     // engine The engine to be notified when a connection request is received
     // t Expected connection type as stream type enumeration
     // addr Address to bind
     // port Port to bind
     // backlog Maximum length of the queue of pending connections, 0 for system maximum
     // prio Thread priority
    inline TcpListener(const char* name, JBEngine* engine, JBStream::Type t,
	const char* addr, int port, unsigned int backlog = 0,
	Thread::Priority prio = Thread::Normal)
	: Thread("TcpListener",prio), String(name),
	m_engine(engine), m_type(t), m_address(addr), m_port(port),
	m_backlog(backlog)
	{}
     // Build a SSL/TLS c2s stream listener
     // engine The engine to be notified when a connection request is received
     // addr Address to bind
     // port Port to bind
     // backlog Maximum length of the queue of pending connections, 0 for system maximum
     // prio Thread priority
    inline TcpListener(const char* name, JBEngine* engine, const char* context,
	const char* addr, int port, unsigned int backlog = 0,
	Thread::Priority prio = Thread::Normal)
	: Thread("TcpListener",prio), String(name),
	m_engine(engine), m_type(JBStream::c2s), m_address(addr), m_port(port),
	m_backlog(backlog), m_sslContext(context)
	{}
    // Remove from plugin
    virtual ~TcpListener();
protected:
    // Add to plugin. Bind and start listening. Notify the jabber engine
    // on incoming connections
    virtual void run();
    // Terminate the socket. Show an error debug message if context is not null
    void terminateSocket(const char* context = 0);
private:
    JBEngine* m_engine;
    JBStream::Type m_type;
    Socket m_socket;
    String m_address;
    int m_port;
    unsigned int m_backlog;              // Pending connections queue length
    String m_sslContext;                 // SSL/TLS context
};

/*
 * The module
 */
class JBModule : public Module
{
    friend class TcpListener;            // Add/remove to/from list
public:
    JBModule();
    virtual ~JBModule();
    // Inherited methods
    virtual void initialize();
    // Cancel a given listener or all listeners if name is empty
    void cancelListener(const String& name = String::empty());
    // Check if a message was sent by us
    inline bool isModule(const Message& msg) const {
	    String* module = msg.getParam("module");
	    return module && *module == name();
	}
    // Build a Message. Complete module and protocol parameters
    inline Message* message(const char* msg) {
	    Message* m = new Message(msg);
	    complete(*m);
	    return m;
	}
    // Complete module and/or protocol parameters
    inline void complete(Message& msg) {
	    msg.addParam("module",name());
	    msg.addParam("protocol","jabber");
	}
protected:
    // Inherited methods
    virtual bool received(Message& msg, int id);
    virtual void statusParams(String& str);
    virtual void statusDetail(String& str);
    virtual bool commandComplete(Message& msg, const String& partLine,
	const String& partWord);
    virtual bool commandExecute(String& retVal, const String& line);
    // Build a listener from a list of parameters. Add it to the list and start it
    bool buildListener(const String& name, NamedList& p);
    // Add or remove a listener to/from list
    void listener(TcpListener* l, bool add);
private:
    bool m_init;
    ObjList m_handlers;                  // Message handlers list
    String m_domain;                     // Default domain served by the jabber engine
    ObjList m_streamListeners;
};


/*
 * Local data
 */
JBPendingWorker** JBPendingWorker::s_threads = 0;
unsigned int JBPendingWorker::s_threadCount = 0;
Mutex JBPendingWorker::s_mutex(false,"JBPendingWorker");
static bool s_s2sFeatures = true;        // Offer RFC 3920 version=1 and stream features
                                         //  on incoming s2s streams
static bool s_dumpIq = false;            // Dump 'iq' xml string in jabber.iq message
INIT_PLUGIN(JBModule);                   // The module
static YJBEntityCapsList s_entityCaps;
static YJBEngine* s_jabber = 0;

// Commands help
static const char* s_cmdStatus = "  status jabber [stream_name|{c2s|s2s} [remote_jid]]";
static const char* s_cmdCreate = "  jabber create remote_domain [local_domain]";
static const char* s_cmdDropStreamName = "  jabber drop stream_name";
static const char* s_cmdDropStream = "  jabber drop {c2s|s2s|*|all} [remote_jid]";
static const char* s_cmdDropAll = "  jabber drop {stream_name|{c2s|s2s|*|all} [remote_jid]}";
static const char* s_cmdDebug = "  jabber debug stream_name [debug_level|on|off]";

// Commands handled by this module (format module_name command [params])
static const String s_cmds[] = {
    "drop",
    "create",
    "debug",
    ""
};

// Message handlers installed by the module
static const TokenDict s_msgHandler[] = {
    {"resource.subscribe",  JBMessageHandler::ResSubscribe},
    {"resource.notify",     JBMessageHandler::ResNotify},
    {"user.roster",         JBMessageHandler::UserRoster},
    {"user.update",         JBMessageHandler::UserUpdate},
    {"jabber.iq",           JBMessageHandler::JabberIq},
    {"jabber.item",         JBMessageHandler::JabberItem},
    {0,0}
};

// Add xml data parameter to a message
static inline void addValidParam(NamedList& list, const char* param, const char* value)
{
    if (!TelEngine::null(value))
	list.addParam(param,value);
}

// Add xml data parameter to a message
static void addXmlParam(Message& msg, XmlElement* xml)
{
    if (!xml)
	return;
    xml->removeAttribute("xmlns");
    xml->removeAttribute("from");
    xml->removeAttribute("to");
    NamedString* data = new NamedString("data");
    xml->toString(*data);
    msg.addParam(data);
}

// Build a response to an 'iq' get/set event
static inline XmlElement* buildIqResponse(JBEvent* ev, bool ok, XMPPUtils::IqType t,
    XmlTag::Type xmlType, XMPPNamespace::Type ns)
{
    if (ok) {
	if (t == XMPPUtils::IqGet)
	    return ev->buildIqResult(false,XMPPUtils::createElement(xmlType,ns));
	return ev->buildIqResult(false);
    }
    return ev->buildIqError(false,XMPPError::ServiceUnavailable);
}

// Retrieve a presence from a Message
// Build one if not found
// Make sure the 'from' attribute is set
static inline XmlElement* getPresenceXml(Message& msg, const char* from,
    XMPPUtils::Presence presType)
{
    XmlElement* xml = XMPPUtils::getPresenceXml(msg,"xml","data",presType);
    xml->setAttribute("from",from);
    return xml;
}

// Get a space separated word from a buffer
// Return false if empty
static inline bool getWord(String& buf, String& word)
{
    XDebug(&__plugin,DebugAll,"getWord(%s)",buf.c_str());
    int pos = buf.find(" ");
    if (pos >= 0) {
	word = buf.substr(0,pos);
	buf = buf.substr(pos + 1);
    }
    else {
	word = buf;
	buf = "";
    }
    if (!word)
	return false;
    return true;
}

// Add a 'subscription' and, optionally, an 'ask' attribute to a roster item
static inline void addSubscription(XmlElement& dest, const String& sub)
{
    XMPPDirVal d(sub);
    if (d.test(XMPPDirVal::PendingOut))
	dest.setAttribute("ask","subscribe");
    String tmp;
    d.toSubscription(tmp);
    dest.setAttribute("subscription",tmp);
}

// Build a roster item XML element from an array row
static XmlElement* buildRosterItem(NamedList& list, unsigned int index)
{
    String prefix("contact.");
    prefix << index;
    const char* contact = list.getValue(prefix);
    XDebug(&__plugin,DebugAll,"buildRosterItem(%s,%u) contact=%s",
	list.c_str(),index,contact);
    if (TelEngine::null(contact))
	return 0;
    XmlElement* item = new XmlElement("item");
    item->setAttribute("jid",contact);
    prefix << ".";
    ObjList* groups = 0;
    unsigned int n = list.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedString* param = list.getParam(i);
	if (!(param && param->name().startsWith(prefix)))
	    continue;
	String name = param->name();
	name.startSkip(prefix,false);
	if (name == "name")
	    item->setAttributeValid("name",*param);
	else if (name == "subscription")
	    addSubscription(*item,*param);
	else if (name == "groups") {
	    if (!groups)
		groups = param->split(',',false);
	}
	else
	    item->addChild(XMPPUtils::createElement(name,*param));
    }
    if (!item->getAttribute("subscription"))
	addSubscription(*item,String::empty());
    for (ObjList* o = groups ? groups->skipNull() : 0; o; o = o->skipNext()) {
	String* grp = static_cast<String*>(o->get());
	item->addChild(XMPPUtils::createElement("group",*grp));
    }
    TelEngine::destruct(groups);
    return item;
}

// Complete stream type
static void completeStreamType(String& buf, const String& part, bool addAll = false)
{
    static const String t[] = {"c2s","s2s","comp", ""};
    static const String all[] = {"all","*",""};
    for (const String* d = t; !d->null(); d++)
	Module::itemComplete(buf,*d,part);
    if (addAll)
	for (const String* d = all; !d->null(); d++)
	    Module::itemComplete(buf,*d,part);
}

// Retrieve an element's child text
static const String& getChildText(XmlElement& xml, int tag, int ns)
{
    XmlElement* ch = XMPPUtils::findFirstChild(xml,tag,ns);
    return ch ? ch->getText() : String::empty();
}

// Append stream's remote jid/domain(s) to a string
static void fillStreamRemote(String& buf, JBStream& stream, const char* sep = " ")
{
    String tmp;
    if (stream.remote())
	tmp = stream.remote();
    JBServerStream* s = stream.serverStream();
    unsigned int n = s ? s->remoteDomains().count() : 0;
    for (unsigned int i = 0; i < n; i++) {
	NamedString* ns = s->remoteDomains().getParam(i);
	if (ns)
	    tmp.append(ns->name(),sep);
    }
    buf << tmp;
}


/*
 * YJBEntityCapsList
 */
// Load the entity caps file
void YJBEntityCapsList::load()
{
    if (!m_enable)
	return;
    String file;
    getEntityCapsFile(file);
    loadXmlDoc(file,s_jabber);
}

// Notify changes and save the entity caps file
void YJBEntityCapsList::capsAdded(JBEntityCaps* caps)
{
    if (!caps) {
	// TODO: Notify all
	return;
    }
    // Notify
    Message* m = __plugin.message("resource.notify");
    m->addParam("operation","updatecaps");
    m->addParam("id",caps->toString());
    addCaps(*m,*caps);
    Engine::enqueue(m);
    // Save the file
    String file;
    getEntityCapsFile(file);
    saveXmlDoc(file,s_jabber);
}


/*
 * YJBEngine
 */
YJBEngine::YJBEngine()
    : m_c2sTlsRequired(false),
    m_allowUnsecurePlainAuth(false)
{
    m_c2sReceive = new YStreamSetReceive(this,10,"c2s/recv");
    m_c2sProcess = new YStreamSetProcess(this,10,"c2s/process");
    m_s2sReceive = new YStreamSetReceive(this,0,"s2s/recv");
    m_s2sProcess = new YStreamSetProcess(this,0,"s2s/process");
    m_compReceive = new YStreamSetReceive(this,0,"comp/recv");
    m_compProcess = new YStreamSetProcess(this,0,"comp/process");
    // c2s features
    m_c2sFeatures.add(XMPPNamespace::DiscoInfo);
    m_c2sFeatures.add(XMPPNamespace::DiscoItems);
    m_c2sFeatures.add(XMPPNamespace::Roster);
    m_c2sFeatures.add(XMPPNamespace::IqPrivate);
    m_c2sFeatures.add(XMPPNamespace::VCard);
    m_c2sFeatures.add(XMPPNamespace::MsgOffline);
    m_c2sFeatures.add(XMPPNamespace::IqVersion);
    m_c2sFeatures.add(XMPPNamespace::Session);
    m_c2sFeatures.add(XmlTag::Register,XMPPNamespace::Register);
    m_c2sFeatures.m_identities.append(new JIDIdentity("server","im"));
    m_c2sFeatures.updateEntityCaps();
    // Non c2s features
    m_features.add(XMPPNamespace::DiscoInfo);
    m_features.add(XMPPNamespace::DiscoItems);
    m_features.add(XMPPNamespace::VCard);
    m_features.add(XMPPNamespace::MsgOffline);
    m_features.add(XMPPNamespace::IqVersion);
    m_features.m_identities.append(new JIDIdentity("server","im"));
    m_features.updateEntityCaps();
}

YJBEngine::~YJBEngine()
{
}

// (Re)initialize engine
void YJBEngine::initialize(const NamedList* params, bool first)
{
    NamedList dummy("");
    lock();
    if (!params)
	params = &dummy;

    m_allowUnsecurePlainAuth = params->getBoolValue("c2s_allowunsecureplainauth");

    // Serviced domains
    // Check if an existing domain is no longer accepted
    // Terminate all streams having local party the deleted domain
    String domains(params->getValue("domains"));
    domains.toLower();
    ObjList* l = domains.split(',',false);
    // Remove serviced domains
    ObjList* o = l->skipNull();
    for (; o; o = o->skipNext())
	m_domains.remove(o->get()->toString());
    // Terminate streams
    for (o = m_domains.skipNull(); o; o = o->skipNext()) {
	JabberID local(o->get()->toString());
	if (local)
	    dropAll(JBStream::TypeCount,local);
    }
    m_domains.clear();
    // Set domains
    while (0 != (o = l->skipNull()))
	m_domains.append(o->remove(false));
    TelEngine::destruct(l);
    if (m_domains.skipNull()) {
	if (debugAt(DebugAll)) {
	    String tmp;
	    for (ObjList* o = m_domains.skipNull(); o; o = o->skipNext())
		tmp.append(o->get()->toString(),",");
	    DDebug(this,DebugAll,"Configured domains='%s'",tmp.c_str());
	}
    }
    else
	Debug(this,DebugNote,"No domains configured");

    // Restricted resources
    String* res = params->getParam("restricted_resources");
    m_restrictedResources.clear();
    if (res) {
	ObjList* list = res->split(',',false);
	for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
	    String* tmp = static_cast<String*>(o->get());
	    if (!m_restrictedResources.find(*tmp))
		m_restrictedResources.append(new String(*tmp));
	}
	TelEngine::destruct(list);
    }

    if (first) {
	m_dialbackSecret = params->getValue("dialback_secret");
	if (!m_dialbackSecret) {
	    MD5 md5;
	    md5 << String((unsigned int)Time::msecNow());
	    md5 << String(Engine::runId());
	    md5 << String((int)::random());
	    m_dialbackSecret = md5.hexDigest();
	}
    }

    m_c2sTlsRequired = params->getBoolValue("c2s_tlsrequired");

    // Update default remote domain
    if (params->getBoolValue("s2s_tlsrequired"))
	m_remoteDomain.m_flags |= JBStream::TlsRequired;
    else
	m_remoteDomain.m_flags &= ~JBStream::TlsRequired;

    // Allow old style client auth
    m_c2sFeatures.remove(XMPPNamespace::IqAuth);
    if (params->getBoolValue("c2s_oldstyleauth",true))
	m_c2sFeatures.add(XmlTag::Auth,XMPPNamespace::IqAuth);

    // Program name and version to be advertised on request
    if (!m_progName) {
	m_progName = "Yate";
	m_progVersion.clear();
	m_progVersion << Engine::runParams().getValue("version") << "" <<
	    Engine::runParams().getValue("release");
	// TODO: set program name and version for server identities
    }
    unlock();
    JBEngine::initialize(*params);

    // TODO: update stream sets options
}

// Process events
void YJBEngine::processEvent(JBEvent* ev)
{
    if (!(ev && ev->stream())) {
	if (ev && !ev->stream())
	    DDebug(this,DebugStub,"Event (%p,'%s') without stream",ev,ev->name());
	TelEngine::destruct(ev);
	return;
    }
    XDebug(this,DebugInfo,"Processing event (%p,%s)",ev,ev->name());
    switch (ev->type()) {
	case JBEvent::Message:
	    if (!ev->element())
		break;
	    if (!routeInternal(ev))
		JBPendingWorker::add(ev);
	    break;
	case JBEvent::Presence:
	    if (!ev->element())
		break;
	    if (!routeInternal(ev))
		processPresenceStanza(ev);
	    break;
	case JBEvent::Iq:
	    if (!ev->element())
		break;
	    if (!routeInternal(ev))
		JBPendingWorker::add(ev);
	    break;
	case JBEvent::Start:
	    if (ev->stream()->incoming())
		processStartIn(ev);
	    else {
		if (!checkDupId(ev->stream()))
		    ev->stream()->start();
		else
		    ev->stream()->terminate(-1,true,0,XMPPError::InvalidId,"Duplicate stream id");
	    }
	    break;
	case JBEvent::Auth:
	    if (ev->stream()->incoming())
		processAuthIn(ev);
	    break;
	case JBEvent::Bind:
	    processBind(ev);
	    break;
	case JBEvent::Running:
	case JBEvent::Destroy:
	case JBEvent::Terminated:
	    processStreamEvent(ev);
	    break;
	case JBEvent::DbResult:
	    processDbResult(ev);
	    break;
	case JBEvent::DbVerify:
	    processDbVerify(ev);
	    break;
	default:
	    returnEvent(ev,XMPPError::ServiceUnavailable);
	    return;
    }
    TelEngine::destruct(ev);
}

// Build an internal stream name from node name and stream index
void YJBEngine::buildStreamName(String& name, const JBStream* stream)
{
//    name << Engine::nodeName() << "/";
    JBServerEngine::buildStreamName(name,stream);
    if (stream)
	name = String(stream->typeName()) + "/" + name;
}

// Start stream TLS
void YJBEngine::encryptStream(JBStream* stream)
{
    if (!stream)
	return;
    DDebug(this,DebugAll,"encryptStream(%p,'%s')",stream,stream->toString().c_str());
    Message msg("socket.ssl");
    msg.userData(stream);
    msg.addParam("server",String::boolText(stream->incoming()));
    if (stream->incoming())
	msg.addParam("domain",stream->local().domain());
    if (!Engine::dispatch(msg))
	stream->terminate(0,stream->incoming(),0,XMPPError::Internal,"SSL start failure");
}

// Connect an outgoing stream
void YJBEngine::connectStream(JBStream* stream)
{
    if (Engine::exiting() || exiting())
	return;
    if (stream && stream->outgoing())
	(new YJBConnectThread(*stream))->startup();
}

// Build a dialback key
void YJBEngine::buildDialbackKey(const String& id, const String& local,
    const String& remote, String& key)
{
    SHA1 sha(m_dialbackSecret);
    SHA1 shaKey(sha.hexDigest());
    shaKey << local << " " << remote << " " << id;
    key = shaKey.hexDigest();
}

// Check if a domain is serviced by this engine
bool YJBEngine::hasDomain(const String& domain)
{
    if (!domain)
	return false;
    Lock lock(this);
    return 0 != findDomain(domain,true) || 0 != findDomain(domain,false);
}

// Add or remove a component to/from serviced domains and components list
void YJBEngine::setComponent(const String& domain, bool add)
{
    Lock lock(this);
    ObjList* oc = m_components.skipNull();
    for (; oc; oc = oc->skipNext()) {
	String* tmp = static_cast<String*>(oc->get());
	if (*tmp == domain)
	    break;
    }
    ObjList* od = findDomain(domain,false);
    if (add) {
	if (!oc)
	    m_components.append(new String(domain));
	if (!od) {
	    m_dynamicDomains.append(new String(domain));
	    Debug(this,DebugAll,"Added component '%s' to dynamic domains",
		domain.c_str());
	}
    }
    else {
	if (oc)
	    oc->remove();
	if (od) {
	    // TODO: remove streams ?
	    od->remove();
	    Debug(this,DebugAll,"Removed component '%s' from dynamic domains",
		domain.c_str());
	}
    }
}

// Check if a component is serviced by this engine
bool YJBEngine::hasComponent(const String& domain)
{
    Lock lock(this);
    for (ObjList* o = m_components.skipNull(); o; o = o->skipNext()) {
	String* tmp = static_cast<String*>(o->get());
	if (*tmp == domain)
	    return true;
    }
    return false;
}

// Check if a resource name is retricted
bool YJBEngine::restrictedResource(const String& name)
{
    Lock lock(this);
    for (ObjList* o = m_restrictedResources.skipNull(); o; o = o->skipNext()) {
	String* s = static_cast<String*>(o->get());
	if (s->startsWith(name))
	    return true;
    }
    // Check item resources
    for (ObjList* o = m_items.skipNull(); o; o = o->skipNext()) {
	JabberID* jid = static_cast<JabberID*>(o->get());
	if (jid->resource() && jid->resource().startsWith(name))
	    return true;
    }
    return false;
}

// Check if a domain is serviced by a server item
bool YJBEngine::isServerItemDomain(const String& domain)
{
    Lock lock(this);
    for (ObjList* o = m_items.skipNull(); o; o = o->skipNext()) {
	JabberID* jid = static_cast<JabberID*>(o->get());
	if (domain == jid->domain())
	    return true;
    }
    return false;
}

// Internally route c2s <--> comp stanzas
// Return true if handled
bool YJBEngine::routeInternal(JBEvent* ev)
{
    JBStream* s = 0;
    if (ev->stream()->type() == JBStream::s2s) {
	// Incoming on s2s: check if it should be routed to a component
	if (!hasComponent(ev->to().domain()))
	    return false;
	String comp;
	getSubDomain(comp,ev->to().domain());
	if (comp) {
	    String local = ev->to().domain().substr(comp.length() + 1);
	    s = findServerStream(local,ev->to().domain(),true);
	}
    }
    else if (ev->stream()->type() == JBStream::comp) {
	// Incoming on comp: check if it should be routed to a remote domain
	if (hasDomain(ev->to().domain()))
	    return false;
	s = findServerStream(ev->from().domain(),ev->to().domain(),true);
    }
    else
	return false;

    DDebug(this,DebugAll,"routeInternal() src=%s from=%s to=%s stream=%p",
	ev->stream()->typeName(),ev->from().c_str(),ev->to().c_str(),s);
    if (s) {
	XmlElement* xml = ev->releaseXml();
	bool ok = false;
	if (xml) {
	    xml->removeAttribute(XmlElement::s_ns);
	    ok = s->sendStanza(xml);
	}
	if (!ok)
	    ev->sendStanzaError(XMPPError::Internal);
    }
    else
	ev->sendStanzaError(XMPPError::NoRemote,0,XMPPError::TypeCancel);
    return true;
}

// Process 'user.roster' message
void YJBEngine::handleUserRoster(Message& msg)
{
    String* what = msg.getParam("notify");
    if (TelEngine::null(what))
	return;
    JabberID to(msg.getValue("username"));
    if (!to.node())
	return;
    const char* contact = msg.getValue("contact");
    Debug(this,DebugAll,"Processing %s from=%s to=%s notify=%s",
	msg.c_str(),to.c_str(),contact,what->c_str());
    XmlElement* item = 0;
    if (*what == "update")
	item = buildRosterItem(msg,1);
    else if (*what == "delete") {
	JabberID c(contact);
	if (!c.node())
	    return;
	item = new XmlElement("item");
	item->setAttribute("jid",c.bare());
	item->setAttribute("subscription","remove");
    }
    if (!item)
	return;
    XmlElement* query = XMPPUtils::createElement(XmlTag::Query,XMPPNamespace::Roster);
    query->addChild(item);
    XmlElement* xml = XMPPUtils::createIq(XMPPUtils::IqSet,0,0,
	String((unsigned int)msg.msgTime().msec()));
    xml->addChild(query);
    // RFC 3920bis 2.2:
    // Send roster pushes to clients that requested the roster
    ObjList* streams = findClientStreams(true,to,JBStream::RosterRequested);
    sendStanza(xml,streams);
}

// Process 'user.update' messages
void YJBEngine::handleUserUpdate(Message& msg)
{
    JabberID user(msg.getValue("user"));
    if (!user)
	return;
    String* notif = msg.getParam("notify");
    if (TelEngine::null(notif) || *notif != "delete")
	return;
    // Don't set any error string: the stream might not be authenticated
    terminateClientStreams(user,XMPPError::Reg);
}

// Process 'jabber.iq' messages
bool YJBEngine::handleJabberIq(Message& msg)
{
    JabberID from(msg.getValue("from"));
    JabberID to(msg.getValue("to"));
    if (!from.resource())
	from.resource(msg.getValue("from_instance"));
    if (!to.resource())
	to.resource(msg.getValue("to_instance"));
    if (!(from && to))
	return false;
    Debug(this,DebugAll,"Processing %s from=%s to=%s",
	msg.c_str(),from.c_str(),to.c_str());
    JBStream* stream = 0;
    if (hasDomain(to.domain()) && !hasComponent(to.domain())) {
	stream = findClientStream(true,to);
	if (!(stream && stream->flag(JBStream::AvailableResource)))
	    TelEngine::destruct(stream);
    }
    else
	stream = getServerStream(from,to);
    if (!stream)
	return false;
    XmlElement* xml = XMPPUtils::getXml(msg);
    bool ok = (xml != 0);
    if (xml) {
	xml->removeAttribute("xmlns");
	xml->setAttribute("from",from);
	xml->setAttribute("to",to);
	ok = stream->sendStanza(xml);
    }
    TelEngine::destruct(stream);
    return ok;
}

// Process 'resource.subscribe' messages
bool YJBEngine::handleResSubscribe(Message& msg)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    XMPPUtils::Presence presType = XMPPUtils::presenceType(*oper);
    if (presType != XMPPUtils::Subscribe && presType != XMPPUtils::Unsubscribe)
	return false;
    JabberID from(msg.getValue("subscriber"));
    JabberID to(msg.getValue("notifier"));
    if (!(from.node() && to.bare()))
	return false;
    Debug(this,DebugAll,"Processing %s from=%s to=%s oper=%s",
	msg.c_str(),from.bare().c_str(),to.bare().c_str(),oper->c_str());
    XmlElement* xml = getPresenceXml(msg,from.bare(),presType);
    bool ok = false;
    if (hasDomain(to.domain()) && !hasComponent(to.domain())) {
	xml->removeAttribute("to");
	// RFC 3921: (un)subscribe requests are sent only to available resources
	String* instance = msg.getParam("instance");
	if (!TelEngine::null(instance)) {
	    to.resource(*instance);
	    JBClientStream* s = findClientStream(true,to);
	    if (s && s->flag(JBStream::AvailableResource))
		ok = s->sendStanza(xml);
	    TelEngine::destruct(s);
	}
	else {
	    ObjList* list = findClientStreams(true,to,JBStream::AvailableResource);
	    ok = sendStanza(xml,list);
	}
    }
    else {
	// Make sure the 'to' attribute is correct
	xml->setAttribute("to",to.bare());
	JBStream* stream = getServerStream(from,to);
	ok = stream && stream->sendStanza(xml);
	TelEngine::destruct(stream);
    }
    TelEngine::destruct(xml);
    return ok;
}

// Process 'resource.notify' messages
bool YJBEngine::handleResNotify(Message& msg)
{
    String* oper = msg.getParam("operation");
    if (TelEngine::null(oper))
	return false;
    JabberID from(msg.getValue("from"));
    JabberID to(msg.getValue("to"));
    if (!(from.node() && to.node()))
	return false;
    Debug(this,DebugAll,"Processing %s from=%s to=%s oper=%s",
	msg.c_str(),from.c_str(),to.c_str(),oper->c_str());
    XmlElement* xml = 0;
    bool c2s = hasDomain(to.domain()) && !hasComponent(to.domain());
    bool online = (*oper == "online" || *oper == "update");
    if (online  || *oper == "offline" || *oper == "delete") {
	if (!from.resource())
	    from.resource(msg.getValue("from_instance"));
	if (!from.resource() && online)
	    return false;
	if (!to.resource())
	    to.resource(msg.getValue("to_instance"));
	xml = getPresenceXml(msg,from,
	    online ? XMPPUtils::PresenceNone : XMPPUtils::Unavailable);
    }
    else {
	bool sub = (*oper == "subscribed");
	if (sub || *oper == "unsubscribed") {
	    // Don't sent (un)subscribed to clients
	    if (c2s)
		return false;
	    // Make sure 'to' is a bare jid
	    to.resource("");
	    xml = getPresenceXml(msg,from.bare(),
		sub ? XMPPUtils::Subscribed : XMPPUtils::Unsubscribed);
	}
	else if (*oper == "probe") {
	    // Don't sent probe to clients
	    if (c2s)
		return false;
	    // Make sure 'to' is a bare jid
	    to.resource("");
	    xml = getPresenceXml(msg,from.bare(),XMPPUtils::Probe);
	}
	else if (*oper == "error") {
	    if (!from.resource())
		from.resource(msg.getValue("from_instance"));
	    if (!to.resource())
		to.resource(msg.getValue("to_instance"));
	    if (!(from.resource() && to.resource()))
		return false;
	    xml = getPresenceXml(msg,from,XMPPUtils::PresenceError);
	}
	else
	    return false;
    }
    bool ok = false;
    if (c2s) {
	// We don't need to send the 'to' attribute
	// Remove it to make sure we don't send a wrong value (trust the
	// 'to' parameter received with the message)
	xml->removeAttribute("to");
	// Ignore streams whose clients didn't sent the initial presence
	if (to.resource()) {
	    JBClientStream* s = findClientStream(true,to);
	    ok = s && s->flag(JBStream::AvailableResource) && s->sendStanza(xml);
	    TelEngine::destruct(s);
	}
	else {
	    ObjList* list = findClientStreams(true,to,JBStream::AvailableResource);
	    ok = sendStanza(xml,list);
	}
    }
    else {
	// Make sure the 'to' attribute is correct
	xml->setAttribute("to",to);
	JBStream* stream = getServerStream(from,to);
	ok = stream && stream->sendStanza(xml);
	TelEngine::destruct(stream);
    }
    TelEngine::destruct(xml);
    return ok;
}

// Process 'msg.execute' messages
bool YJBEngine::handleMsgExecute(Message& msg)
{
    JabberID caller(msg.getValue("caller"));
    JabberID called(msg.getValue("called"));
    if (!caller.resource())
	caller.resource(msg.getValue("caller_instance"));
    Debug(this,DebugAll,"Processing %s caller=%s called=%s",
	msg.c_str(),caller.c_str(),called.c_str());
    if (hasDomain(called.domain()) && !hasComponent(called.domain())) {
	// RFC 3921 11.1: Broadcast chat only to clients with non-negative resource priority
	bool ok = false;
	unsigned int n = msg.getIntValue("instance.count");
	if (n) {
	    ObjList resources;
	    for (unsigned int i = 1; i <= n; i++) {
		String prefix("instance.");
		prefix << i;
		String* tmp = msg.getParam(prefix);
		if (TelEngine::null(tmp))
		    continue;
		resources.append(new String(tmp->c_str()));
	    }
	    ObjList* streams = findClientStreams(true,called,resources,
		JBStream::AvailableResource | JBStream::PositivePriority);
	    if (streams) {
		XmlElement* xml = XMPPUtils::getChatXml(msg);
		if (xml) {
		    xml->setAttribute("from",caller);
		    xml->setAttribute("to",called.bare());
		}
		ok = sendStanza(xml,streams);
	    }
	}
	else {
	    // Directed chat
	    if (!called.resource())
		called.resource(msg.getValue("called_instance"));
	    JBClientStream* stream = called.resource() ? findClientStream(true,called) : 0;
	    ok = stream && stream->flag(JBStream::AvailableResource);
	    if (ok) {
		XmlElement* xml = XMPPUtils::getChatXml(msg);
		if (xml) {
		    xml->setAttribute("from",caller);
		    xml->setAttribute("to",called);
		    ok = stream->sendStanza(xml);
		}
		else
		    ok = false;
	    }
	    TelEngine::destruct(stream);
	}
	return ok;
    }

    // Remote domain
    JBStream* stream = s_jabber->getServerStream(caller,called);
    if (!stream)
	return false;
    bool ok = false;
    XmlElement* xml = XMPPUtils::getChatXml(msg);
    if (xml) {
	if (!called.resource())
	    called.resource(msg.getValue("called_instance"));
	xml->setAttribute("from",caller);
	xml->setAttribute("to",called);
	ok = stream->sendStanza(xml);
    }
    TelEngine::destruct(stream);
    return ok;
}

// Process 'jabber.item' messages
// Add or remove server items and/or serviced domains
bool YJBEngine::handleJabberItem(Message& msg)
{
    JabberID jid = msg.getValue("jid");
    if (!jid)
	return false;

    Lock lock(this);
    ObjList* o = m_items.skipNull();
    for (; o; o = o->skipNext()) {
	JabberID* tmp = static_cast<JabberID*>(o->get());
	if (*tmp == jid)
	    break;
    }
    bool remove = msg.getBoolValue("remove");
    if ((o != 0) != remove)
	return true;
    ObjList* dynamic = findDomain(jid.domain(),false);
    if (remove) {
	o->remove();
	Debug(this,DebugAll,"Removed item '%s'",jid.c_str());
	if (dynamic && !isServerItemDomain(jid.domain())) {
	     // TODO: remove streams ?
	     dynamic->remove();
	     Debug(this,DebugAll,"Removed item '%s' from serviced domains",jid.c_str());
	}
	return true;
    }
    if (dynamic && hasComponent(jid.domain())) {
	Debug(this,DebugNote,
	    "Request to add server item '%s' while already having a component",
	    jid.c_str());
	return false;
    }
    m_items.append(new JabberID(jid));
    Debug(this,DebugAll,"Added item '%s'",jid.c_str());
    if (!dynamic) {
	m_dynamicDomains.append(new String(jid.domain()));
	Debug(this,DebugAll,"Added item '%s' to serviced domains",jid.c_str());
    }
    return true;
}

// Handle 'presence' stanzas
// s2s: Destination domain was already checked by the lower layer
// The given event is always valid and carry a valid stream and xml element
void YJBEngine::processPresenceStanza(JBEvent* ev)
{
    Debug(this,DebugAll,"Processing (%p,%s) type=%s from=%s to=%s stream=%s",
	ev->element(),ev->element()->tag(),ev->stanzaType().c_str(),
	ev->from().c_str(),ev->to().c_str(),ev->stream()->typeName());
    JBServerStream* s2s = ev->serverStream();
    JBClientStream* c2s = ev->clientStream();
    if (!(c2s || s2s)) {
	Debug(this,DebugNote,
	    "processPresenceStanza(%s) not handled for stream type '%s'",
	    ev->stanzaType().c_str(),lookup(ev->stream()->type(),JBStream::s_typeName));
	return;
    }
    if (c2s && c2s->outgoing()) {
	DDebug(this,DebugStub,
	    "processPresenceStanza(%s) not implemented for outgoing client streams",
	    ev->stanzaType().c_str());
	ev->sendStanzaError(XMPPError::ServiceUnavailable);
    }
    XMPPUtils::Presence pres = XMPPUtils::presenceType(ev->stanzaType());
    bool online = false;
    String capsId;
    switch (pres) {
	case XMPPUtils::PresenceNone:
	    online = true;
	    // Update caps
	    s_entityCaps.processCaps(capsId,ev->element(),ev->stream(),ev->to(),ev->from());
	case XMPPUtils::Unavailable:
	    if (c2s) {
		bool offlinechat = false;
		if (!ev->to()) {
		    if (!c2s->remote().resource())
			break;
		    // Presence broadcast
		    int prio = XMPPUtils::priority(*ev->element());
		    offlinechat = c2s->setAvailableResource(online,prio >= 0) &&
			online && c2s->flag(JBStream::PositivePriority);
		    notifyPresence(*c2s,online,ev->element(),capsId);
		}
		else
		    notifyPresence(ev->from(),ev->to(),online,ev->element(),
			false,hasDomain(ev->to().domain()),capsId);
		if (offlinechat) {
		    Message* m = jabberFeature(0,XMPPNamespace::MsgOffline,
			JBStream::c2s,ev->from(),0,"query");
		    if (m && Engine::dispatch(*m)) {
			unsigned int n = m->length();
			bool ok = false;
			for (unsigned int i = 0; i < n; i++) {
			    NamedString* p = m->getParam(i);
			    if (p && p->name() == "xml") {
				XmlElement* xml = XMPPUtils::getXml(p);
				if (xml)
				    ok = c2s->sendStanza(xml) || ok;
			    }
			}
			if (ok)
			    Engine::enqueue(jabberFeature(0,XMPPNamespace::MsgOffline,
				JBStream::c2s,ev->from(),0,"delete"));
		    }
		    TelEngine::destruct(m);
		}
		return;
	    }
	    if (s2s) {
		notifyPresence(ev->from(),ev->to(),online,ev->element(),true,false,capsId);
		return;
	    }
	    break;
	case XMPPUtils::Subscribe:
	case XMPPUtils::Unsubscribe:
	    if (ev->to()) {
		Message* m = __plugin.message("resource.subscribe");
		m->addParam("operation",ev->stanzaType());
		m->addParam("subscriber",ev->from().bare());
		m->addParam("subscriber_local",String::boolText(c2s != 0));
		m->addParam("notifier",ev->to().bare());
		m->addParam("notifier_local",String::boolText(hasDomain(ev->to().domain())));
		addXmlParam(*m,ev->element());
		Engine::enqueue(m);
		return;
	    }
	    break;
	case XMPPUtils::Subscribed:
	case XMPPUtils::Unsubscribed:
	case XMPPUtils::Probe:
	case XMPPUtils::PresenceError:
	    if (ev->to() || pres == XMPPUtils::PresenceError) {
		Message* m = __plugin.message("resource.notify");
		m->addParam("operation",ev->stanzaType());
		m->addParam("from",ev->from().bare());
		m->addParam("from_local",String::boolText(c2s != 0));
		if (ev->to()) {
		    m->addParam("to",ev->to().bare());
		    m->addParam("to_local",String::boolText(hasDomain(ev->to().domain())));
		}
		if (pres == XMPPUtils::PresenceError) {
		    if (ev->from().resource())
			m->addParam("from_instance",ev->from().resource());
		    if (ev->to().resource())
			m->addParam("to_instance",ev->to().resource());
		}
		addXmlParam(*m,ev->element());
		Engine::enqueue(m);
		return;
	    }
	    break;
    }
    ev->sendStanzaError(XMPPError::ServiceUnavailable);
}

// Retrieve a subdomain of a serviced domain
void YJBEngine::getSubDomain(String& subdomain, const String& domain)
{
    Lock lock(this);
    for (ObjList* o = m_domains.skipNull(); o; o = o->skipNext()) {
	String cmp("." + o->get()->toString());
	if (domain.endsWith(cmp) && domain.length() > cmp.length()) {
	    subdomain = domain.substr(0,domain.length() - cmp.length());
	    return;
	}
    }
}

// Process a stream start element received by an incoming stream
// The given event is always valid and carry a valid stream
// Set local domain and stream features to advertise to remote party
void YJBEngine::processStartIn(JBEvent* ev)
{
    static const char* node = "http://yate.null.ro/yate/server/caps";

    JBServerStream* comp = ev->serverStream();
    if (comp && comp->type() == JBStream::comp) {
	String sub;
	if (ev->from() && !ev->from().node() && !ev->from().resource())
	    getSubDomain(sub,ev->from().domain());
	if (!sub) {
	    comp->terminate(-1,true,0,XMPPError::HostUnknown);
	    return;
	}
	String local(ev->from().substr(sub.length() + 1));
	bool isItem = isServerItemDomain(ev->from().domain());
	if (isItem || findServerStream(local,ev->from(),false)) {
	    if (isItem)
		Debug(this,DebugNote,"Component request for server item domain '%s'",
		    ev->from().domain().c_str());
	    comp->terminate(-1,true,0,XMPPError::Conflict);
	    return;
	}
	// Add component to serviced domains
	setComponent(ev->from(),true);
	comp->startComp(local,ev->from());
	return;
    }

    // Set c2s stream TLS required flag
    if (ev->stream()->type() == JBStream::c2s)
	ev->stream()->setTlsRequired(m_c2sTlsRequired);

    XMPPFeatureList features;

    // Stream version is not 1
    if (!ev->stream()->flag(JBStream::StreamRemoteVer1)) {
	XMPPError::Type error = XMPPError::NoError;
	if (ev->stream()->type() == JBStream::c2s) {
	    lock();
	    bool ok = m_c2sFeatures.get(XMPPNamespace::IqAuth);
	    unlock();
	    if (ok) {
		if (ev->stream()->flag(JBStream::StreamTls) ||
		    !ev->stream()->flag(JBStream::TlsRequired))
		    features.add(XmlTag::Auth,XMPPNamespace::IqAuth,true);
		else
		    error = XMPPError::EncryptionRequired;
	    }
	    else
		error = XMPPError::UnsupportedVersion;
	}
	if (error == XMPPError::NoError)
	    ev->stream()->start(&features);
	else
	    ev->stream()->terminate(-1,true,0,error);
	return;
    }

    bool s2sVer1 = s_s2sFeatures;

    // Set stream features
    // Add TLS if not secured
    if (!ev->stream()->flag(JBStream::StreamSecured) &&
	(ev->stream()->type() == JBStream::c2s || s2sVer1))
	features.add(XmlTag::Starttls,XMPPNamespace::Tls,
	    ev->stream()->flag(JBStream::TlsRequired));
    // Done for s2s streams
    if (ev->stream()->type() == JBStream::s2s) {
	ev->stream()->start(&features,0,s2sVer1);
	return;
    }
    XMPPFeature* tls = features.get(XMPPNamespace::Tls);
    bool addReg = ev->stream()->type() == JBStream::c2s &&
	m_c2sFeatures.get(XMPPNamespace::Register);
    bool addCaps = false;
    if (!(tls && tls->required())) {
	addCaps = true;
	// Add SASL auth if stream is not authenticated
	if (!ev->stream()->flag(JBStream::StreamAuthenticated) &&
	    ev->stream()->type() == JBStream::c2s) {
	    int mech = XMPPUtils::AuthMD5;
	    if (ev->stream()->flag(JBStream::StreamTls) || m_allowUnsecurePlainAuth)
		mech |= XMPPUtils::AuthPlain;
	    features.add(new XMPPFeatureSasl(mech,true));
	}
	// Add register if enabled
	if (addReg)
	    features.add(XmlTag::Register,XMPPNamespace::Register);
	XMPPFeature* sasl = features.get(XMPPNamespace::Sasl);
	if (!(sasl && sasl->required())) {
	    if (ev->stream()->type() == JBStream::c2s) {
		// TLS and/or SASL are missing or not required: add bind
		features.add(XmlTag::Bind,XMPPNamespace::Bind,true);
	    }
	}
    }
    else if (addReg && tls && !tls->required()) {
	// Stream not secured, TLS not required: add register
	features.add(XmlTag::Register,XMPPNamespace::Register);
    }
    ev->releaseStream();
    XmlElement* caps = 0;
    if (ev->stream()->type() == JBStream::c2s && addCaps)
	caps = XMPPUtils::createEntityCaps(m_features.m_entityCapsHash,node);
    ev->stream()->start(&features,caps);
}

// Process Auth events from incoming streams
// The given event is always valid and carry a valid stream
void YJBEngine::processAuthIn(JBEvent* ev)
{
    UserAuthMessage* m = new UserAuthMessage(ev);
    XMPPError::Type error = XMPPError::NoError;
    ev->stream()->lock();
    if (ev->stream()->type() == JBStream::c2s) {
	bool allowPlain = ev->stream()->flag(JBStream::StreamTls) ||
	    m_allowUnsecurePlainAuth;
	while (true) {
	    // Stream is using SASL auth
	    if (ev->stream()->m_sasl) {
		XDebug(this,DebugAll,"processAuthIn(%s) c2s sasl",ev->stream()->name());
		if (ev->stream()->m_sasl->m_plain && !allowPlain) {
		    error = XMPPError::EncryptionRequired;
		    break;
		}
		if (ev->stream()->m_sasl->m_params) {
		    m->copyParams(*(ev->stream()->m_sasl->m_params));
		    // Override username: set it to bare jid
		    String* user = ev->stream()->m_sasl->m_params->getParam("username");
		    if (!TelEngine::null(user))
			m->setParam("username",*user + "@" + ev->stream()->local().domain());
		}
		break;
	    }
	    // Check non SASL request
	    XmlElement* q = ev->child();
	    if (q) {
		int t,ns;
		if (XMPPUtils::getTag(*q,t,ns)) {
		    if (t != XmlTag::Query || ns != XMPPNamespace::IqAuth) {
			error = XMPPError::ServiceUnavailable;
			break;
		    }
		    XDebug(this,DebugAll,"processAuthIn(%s) c2s non sasl",ev->stream()->name());
		    JabberID user(getChildText(*q,XmlTag::Username,XMPPNamespace::IqAuth),
			ev->stream()->local().domain(),
			getChildText(*q,XmlTag::Resource,XMPPNamespace::IqAuth));
		    if (!user.resource()) {
			error = XMPPError::NotAcceptable;
			break;
		    }
		    if (user.bare())
			m->addParam("username",user.bare());
		    const String& pwd = getChildText(*q,XmlTag::Password,XMPPNamespace::IqAuth);
		    if (pwd) {
			if (allowPlain)
			    m->addParam("password",pwd);
			else {
			    error = XMPPError::EncryptionRequired;
			    break;
			}
		    }
		    else {
			const String& d = getChildText(*q,XmlTag::Digest,XMPPNamespace::IqAuth);
			if (d)
			    m->addParam("digest",d);
		    }
		    // Make sure the resource is unique
		    if (!bindingResource(user)) {
			error = XMPPError::Conflict;
			break;
		    }
		    else
			m->m_bindingUser = user;
		    m->addParam("instance",user.resource());
		    break;
		}
	    }
	    error = XMPPError::Internal;
	    break;
	}
    }
    else if (ev->stream()->type() == JBStream::comp) {
	XDebug(this,DebugAll,"processAuthIn(%s) component handshake",ev->stream()->name());
	m->setParam("username",ev->stream()->remote());
	m->setParam("handshake",ev->text());
    }
    ev->stream()->unlock();
    if (error == XMPPError::NoError)
	Engine::enqueue(m);
    else {
	ev->releaseStream();
	ev->stream()->authenticated(false,String::empty(),error,0,ev->id());
	TelEngine::destruct(m);
    }
}

// Process Bind events
// The given event is always valid and carry a valid stream
void YJBEngine::processBind(JBEvent* ev)
{
    JBClientStream* c2s = ev->clientStream();
    if (!(c2s && c2s->incoming() && ev->child())) {
	ev->sendStanzaError(XMPPError::ServiceUnavailable);
	return;
    }
    c2s->lock();
    JabberID jid(c2s->remote());
    c2s->unlock();
    jid.resource(getChildText(*ev->child(),XmlTag::Resource,XMPPNamespace::Bind));
    if (jid.resource()) {
	// Check if the user and resource are in bind process
	if (bindingResource(jid)) {
	    // Not binding: check if already bound
	    ObjList res;
	    res.append(new String(jid.resource()));
	    ObjList* list = findClientStreams(true,jid,res);
	    if (list) {
		for (ObjList* o = list->skipNull(); o; o = o->skipNext()) {
		    JBClientStream* s = static_cast<JBClientStream*>(o->get());
		    if (s != c2s) {
			removeBindingResource(jid);
			jid.resource("");
			break;
		    }
		}
		TelEngine::destruct(list);
	    }
	}
	else
	    jid.resource("");
    }
    if (!jid.resource()) {
	for (int i = 0; i < 3; i++) {
	    MD5 md5(c2s->id());
	    jid.resource(md5.hexDigest());
	    if (bindingResource(jid))
		break;
	    jid.resource("");
	}
    }
    bool ok = false;
    if (jid.resource()) {
	Message* m = userRegister(*c2s,true,jid.resource());
	ok = Engine::dispatch(m);
	TelEngine::destruct(m);
    }
    if (ok)
	c2s->bind(jid.resource(),ev->id());
    else
	ev->sendStanzaError(XMPPError::NotAuthorized);
    removeBindingResource(jid);
}

// Process stream Running, Destroy, Terminated events
// The given event is always valid and carry a valid stream
void YJBEngine::processStreamEvent(JBEvent* ev)
{
    XDebug(this,DebugAll,"processStreamEvent(%p,%s)",ev,ev->name());
    JBStream* s = ev->stream();
    bool in = s->incoming();
    bool reg = ev->type() == JBEvent::Running;
    Message* m = 0;
    if (in) {
	if (reg) {
	    // Client streams are registered when a resource is bound to the stream
	    if (s->type() != JBStream::c2s)
		m = userRegister(*s,true);
	}
	else {
	    bool changed = s->setAvailableResource(false);
	    s->setRosterRequested(false);
	    if (s->type() == JBStream::c2s) {
		// Notify 'offline' for client streams that forgot to send 'unavailable'
		if (changed && s->remote().resource())
		    notifyPresence(*(static_cast<JBClientStream*>(s)),false,0,String::empty());
		// Unregister
		m = userRegister(*s,false,s->remote().resource());
	    }
	    else {
		// TODO: notify offline for all users in remote domain
		m = userRegister(*s,false);
	    }
	    // Remove component from serviced domain
	    if (ev->stream()->type() == JBStream::comp)
		setComponent(ev->stream()->remote(),false);
	}
    }
    else {
	// Notify verify failure on dialback streams
	if (!reg) {
	    JBServerStream* s2s = ev->serverStream();
	    NamedString* db = s2s ? s2s->takeDb() : 0;
	    if (db) {
		// See XEP 0220 2.4
		notifyDbVerifyResult(s2s->local(),s2s->remote(),db->name(),
		    XMPPError::RemoteTimeout);
		TelEngine::destruct(db);
	    }
	}
	m = __plugin.message("user.notify");
	m->addParam("account",s->name());
	if (s->type() == JBStream::c2s)
	    m->addParam("username",s->local().node());
	m->addParam("server",s->local().domain());
	m->addParam("jid",s->local());
	m->addParam("registered",String::boolText(reg));
	if (!reg && ev->text())
	    m->addParam("error",ev->text());
	bool restart = (s->state() != JBStream::Destroy && !s->flag(JBStream::NoAutoRestart));
	m->addParam("autorestart",String::boolText(restart));
    }
    Engine::enqueue(m);
}

// Process stream DbResult events
// The given event is always valid and carry a valid stream
void YJBEngine::processDbResult(JBEvent* ev)
{
    JBServerStream* stream = ev->serverStream();
    const char* id = stream ? stream->id().c_str() : 0;
    if (id && ev->text() && stream && ev->to() && hasDomain(ev->to()) && ev->from()) {
	// Check if we already have a stream to the remote domain
	// Build a dialback only outgoing stream if found
	JBServerStream* s = findServerStream(ev->to(),ev->from(),true);
	bool dbOnly = (s != 0);
	TelEngine::destruct(s);
	s = createServerStream(ev->to(),ev->from(),id,ev->text(),dbOnly);
	if (s) {
	    TelEngine::destruct(s);
	    return;
	}
    }
    Debug(this,DebugNote,
	"Failed to authenticate dialback request from=%s to=%s id=%s key=%s",
	ev->from().c_str(),ev->to().c_str(),id,ev->text().c_str());
    if (stream)
	stream->sendDbResult(ev->to(),ev->from(),XMPPError::RemoteConn);
}

// Process stream DbVerify events
// The given event is always valid and carry a valid stream
void YJBEngine::processDbVerify(JBEvent* ev)
{
    JBServerStream* stream = ev->serverStream();
    if (!(stream && ev->element()))
	return;
    String id(ev->element()->getAttribute("id"));
    // Incoming: this is a verify request for an outgoing stream
    if (stream->incoming()) {
	String key;
	if (id)
	    buildDialbackKey(id,ev->to(),ev->from(),key);
	if (key && key == ev->element()->getText())
	    stream->sendDbVerify(ev->to(),ev->from(),id);
	else
	    stream->sendDbVerify(ev->to(),ev->from(),id,XMPPError::NotAuthorized);
	return;
    }
    // Outgoing: we have an incoming stream to authenticate
    // Remove the dialback request from the stream and check it
    NamedString* db = stream->takeDb();
    if (id && db && db->name() == id) {
	int r = XMPPUtils::decodeDbRsp(ev->element());
	// Adjust the response. See XEP 0220 2.4
	if (r == XMPPError::ItemNotFound || r == XMPPError::HostUnknown)
	    r = XMPPError::NoRemote;
	notifyDbVerifyResult(ev->to(),ev->from(),id,(XMPPError::Type)r);
    }
    TelEngine::destruct(db);
    // Terminate dialback only streams
    if (stream->dialback())
	stream->terminate(-1,true,0);
}

// Process all incoming jabber:iq:roster stanzas
// The given event is always valid and carry a valid element
XmlElement* YJBEngine::processIqRoster(JBEvent* ev, JBStream::Type sType,
    XMPPUtils::IqType t)
{
    if (sType != JBStream::c2s) {
	Debug(this,DebugInfo,"processIqRoster(%p) type=%s on non-client stream",
	    ev,ev->stanzaType().c_str());
	// Roster management not allowed from other servers
	if (t == XMPPUtils::IqGet && t == XMPPUtils::IqSet)
	    return ev->buildIqError(false,XMPPError::NotAllowed);
	return 0;
    }
    // Ignore responses
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet)
	return 0;
    DDebug(this,DebugInfo,"processIqRoster type=%s",ev->stanzaType().c_str());
    Message* m = jabberFeature(ev->releaseXml(),XMPPNamespace::Roster,sType,ev->from(),ev->to());
    bool ok = Engine::dispatch(m);
    XmlElement* rsp = XMPPUtils::getXml(*m,"response");
    TelEngine::destruct(m);
    if (rsp)
	return rsp;
    return buildIqResponse(ev,ok,t,XmlTag::Query,XMPPNamespace::Roster);
}

// Process all incoming vcard-temp with target in our domain(s)
// The given event is always valid and carry a valid element
// Return the response
// XEP-0054 vcard-temp
XmlElement* YJBEngine::processIqVCard(JBEvent* ev, JBStream::Type sType,
    XMPPUtils::IqType t)
{
    DDebug(this,DebugAll,"processIqVCard(%p) type=%s from=%s",
	ev,ev->stanzaType().c_str(),ev->from().c_str());
    // Ignore responses
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet)
	return 0;
    // Make sure we have a 'from'
    if (!ev->from().bare())
	return ev->buildIqError(false,XMPPError::ServiceUnavailable);
    Message* m = 0;
    if (t == XMPPUtils::IqSet) {
	// Only the connected client can set its vcard
	if (sType != JBStream::c2s)
	    return ev->buildIqError(false,XMPPError::ServiceUnavailable);
	if (ev->to() && ev->to() != ev->from().domain())
	    return ev->buildIqError(false,XMPPError::ServiceUnavailable);
	m = jabberFeature(ev->releaseXml(),XMPPNamespace::VCard,sType,ev->from());
    }
    else if (!ev->to() || ev->to() == ev->from().domain())
	m = jabberFeature(ev->releaseXml(),XMPPNamespace::VCard,sType,ev->from());
    else
	m = jabberFeature(ev->releaseXml(),XMPPNamespace::VCard,sType,ev->from(),ev->to());
    bool ok = Engine::dispatch(m);
    XmlElement* rsp = XMPPUtils::getXml(*m,"response");
    TelEngine::destruct(m);
    if (rsp)
	return rsp;
    return buildIqResponse(ev,ok,t,XmlTag::VCard,XMPPNamespace::VCard);
}

// Process all incoming jabber:iq:private
// The given event is always valid and carry a valid element
// Return the response
// XEP-0049 Private XML storage
XmlElement* YJBEngine::processIqPrivate(JBEvent* ev, JBStream::Type sType,
    XMPPUtils::IqType t)
{
    if (sType != JBStream::c2s) {
	Debug(this,DebugInfo,"processIqPrivate(%p) type=%s on non-client stream",
	    ev,ev->stanzaType().c_str());
	// User private data management not allowed from other servers
	if (t == XMPPUtils::IqGet || t == XMPPUtils::IqSet)
	    return ev->buildIqError(false,XMPPError::NotAllowed);
	return 0;
    }
    DDebug(this,DebugAll,"processIqPrivate(%p) type=%s from=%s",
	ev,ev->stanzaType().c_str(),ev->from().c_str());
    // Ignore responses
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet)
	return 0;
    // Make sure the client don't request/set another user's private data
    if (ev->to() && ev->to().bare() != ev->from().bare())
	return ev->buildIqError(false,XMPPError::Forbidden);
    if (!ev->from().resource())
	return ev->buildIqError(false,XMPPError::ServiceUnavailable);
    Message* m = jabberFeature(ev->releaseXml(),XMPPNamespace::IqPrivate,sType,ev->from());
    bool ok = Engine::dispatch(m);
    XmlElement* rsp = XMPPUtils::getXml(*m,"response");
    TelEngine::destruct(m);
    if (rsp)
	return rsp;
    return buildIqResponse(ev,ok,t,XmlTag::Query,XMPPNamespace::IqPrivate);
}

// Process all incoming jabber:iq:register stanzas
// The given event is always valid and carry a valid element
// XEP-0077 In-Band Registration
XmlElement* YJBEngine::processIqRegister(JBEvent* ev, JBStream::Type sType,
    XMPPUtils::IqType t, const String& domain, int flags)
{
    if (sType != JBStream::c2s) {
	Debug(this,DebugInfo,"processIqRegister(%p) type=%s on non-client stream",
	    ev,ev->stanzaType().c_str());
	// In-band registration not allowed from other servers
	if (t == XMPPUtils::IqGet || t == XMPPUtils::IqSet)
	    return ev->buildIqError(false,XMPPError::NotAllowed);
	return 0;
    }
    DDebug(this,DebugAll,"processIqRegister(%p) type=%s",ev,ev->stanzaType().c_str());
    // Ignore responses
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet)
	return 0;
    Message* m = jabberFeature(ev->releaseXml(),XMPPNamespace::IqRegister,sType,ev->from());
    m->addParam("stream_domain",domain);
    m->addParam("stream_flags",String(flags));
    Engine::dispatch(m);
    XmlElement* rsp = XMPPUtils::getXml(*m,"response");
    TelEngine::destruct(m);
    return rsp;
}

// Process all incoming jabber:iq:auth stanzas
// The given event is always valid and carry a valid element
XmlElement* YJBEngine::processIqAuth(JBEvent* ev, JBStream::Type sType, XMPPUtils::IqType t,
    int flags)
{
    if (sType != JBStream::c2s) {
	Debug(this,DebugInfo,"processIqAuth(%p) type=%s on non-client stream",
	    ev,ev->stanzaType().c_str());
	// Iq auth not allowed from other servers
	if (t == XMPPUtils::IqGet || t == XMPPUtils::IqSet)
	    return ev->buildIqError(false,XMPPError::NotAllowed);
	return 0;
    }
    DDebug(this,DebugAll,"processIqAuth(%p) type=%s",ev,ev->stanzaType().c_str());
    // Ignore responses
    if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet)
	return 0;
    if (t == XMPPUtils::IqGet)
	return XMPPUtils::createIqAuthOffer(ev->id(),true,
	    m_allowUnsecurePlainAuth || (flags & JBStream::StreamTls));
    return ev->buildIqError(false,XMPPError::ServiceUnavailable);
}

// Handle disco info requests addressed to the server
XmlElement* YJBEngine::discoInfo(JBEvent* ev, JBStream::Type sType)
{
    XMPPError::Type error = XMPPError::NoError;
    if (ev->stanzaType() == "get" &&
	XMPPUtils::isUnprefTag(*ev->child(),XmlTag::Query)) {
	String* node = ev->child()->getAttribute("node");
	bool ok = TelEngine::null(node);
	Lock lock(this);
	if (!ok && ev->to().domain() && node->startsWith(ev->to().domain())) {
	    char c = node->at(ev->to().domain().length());
	    if (!c)
		ok = true;
	    else if (c == '#') {
		String hash(node->substr(ev->to().domain().length() + 1));
		if (sType == JBStream::c2s)
		    ok = hash == m_c2sFeatures.m_entityCapsHash;
		else
		    ok = hash == m_features.m_entityCapsHash;
	    }
	    else
		ok = true;
	}
	if (ok) {
	    if (sType == JBStream::c2s)
		return m_c2sFeatures.buildDiscoInfo(0,0,ev->id());
	    return m_features.buildDiscoInfo(0,0,ev->id());
	}
	error = XMPPError::ItemNotFound;
    }
    else
	error = XMPPError::ServiceUnavailable;
    return ev->buildIqError(false,error);
}

// Handle disco items requests addressed to the server
XmlElement* YJBEngine::discoItems(JBEvent* ev)
{
    XMPPError::Type error = XMPPError::NoError;
    if (ev->stanzaType() == "get" &&
	XMPPUtils::isUnprefTag(*ev->child(),XmlTag::Query)) {
	const char* node = ev->child()->attribute("node");
	if (!node) {
	    XmlElement* query = XMPPUtils::createElement(XmlTag::Query,
		XMPPNamespace::DiscoItems);
	    lock();
	    for (ObjList* o = m_items.skipNull(); o; o = o->skipNext()) {
		String* s = static_cast<String*>(o->get());
		XmlElement* item = new XmlElement("item");
		item->setAttribute("jid",*s);
		query->addChild(item);
	    }
	    unlock();
	    return ev->buildIqResult(false,query);
	}
	else
	    error = XMPPError::ItemNotFound;
    }
    else
	error = XMPPError::ServiceUnavailable;
    return ev->buildIqError(false,error);
}

// Send an XML element to list of client streams.
// The given pointers will be consumed and zeroed
// Return true if the element was succesfully sent on at least one stream
bool YJBEngine::sendStanza(XmlElement*& xml, ObjList*& streams)
{
    DDebug(this,DebugAll,"sendStanza(%p,%p)",xml,streams);
    bool ok = false;
    if (streams && xml) {
	ObjList* o = streams->skipNull();
	while (o) {
	    JBClientStream* stream = static_cast<JBClientStream*>(o->get());
	    o = o->skipNext();
	    // Last stream in the list: send the xml (release it)
	    // Otherwise: send a copy of the element
	    if (!o)
		ok = stream->sendStanza(xml) || ok;
	    else {
		XmlElement* tmp = new XmlElement(*xml);
		ok = stream->sendStanza(tmp) || ok;
	    }
	}
    }
    TelEngine::destruct(streams);
    TelEngine::destruct(xml);
    return ok;
}

// Find a server stream used to send stanzas from local domain to remote
// Build a new one if not found
JBStream* YJBEngine::getServerStream(const JabberID& from, const JabberID& to)
{
    JBServerStream* s = findServerStream(from.domain(),to.domain(),true);
    if (s)
	return s;
    // Avoid streams to internal components or (sub)domains
    if (!hasDomain(from.domain()))
	return 0;
    String comp;
    getSubDomain(comp,to.domain());
    if (comp)
	return 0;
    DDebug(this,DebugAll,"getServerStream(%s,%s) creating s2s",from.c_str(),to.c_str());
    return createServerStream(from.domain(),to.domain());
}

// Notify online/offline presence from client streams
void YJBEngine::notifyPresence(JBClientStream& cs, bool online, XmlElement* xml,
    const String& capsId)
{
    Message* m = __plugin.message("resource.notify");
    m->addParam("operation",online ? "online" : "offline");
    m->addParam("contact",cs.remote().bare());
    m->addParam("instance",cs.remote().resource());
    if (online) {
	if (xml)
	    m->addParam("priority",String(XMPPUtils::priority(*xml)));
	if (capsId)
	    s_entityCaps.addCaps(*m,capsId);
    }
    addXmlParam(*m,xml);
    Engine::enqueue(m);
}

// Notify directed online/offline presence
void YJBEngine::notifyPresence(const JabberID& from, const JabberID& to, bool online,
    XmlElement* xml, bool fromRemote, bool toRemote, const String& capsId)
{
    Message* m = __plugin.message("resource.notify");
    m->addParam("operation",online ? "online" : "offline");
    m->addParam("from",from.bare());
    addValidParam(*m,"from_instance",from.resource());
    if (fromRemote)
	m->addParam("from_local",String::boolText(false));
    m->addParam("to",to.bare());
    addValidParam(*m,"to_instance",to.resource());
    if (toRemote)
	m->addParam("to_local",String::boolText(false));
    if (online) {
	if (xml)
	    m->addParam("priority",String(XMPPUtils::priority(*xml)));
	if (capsId)
	    s_entityCaps.addCaps(*m,capsId);
    }
    addXmlParam(*m,xml);
    Engine::enqueue(m);
}

// Build a jabber.feature message
Message* YJBEngine::jabberFeature(XmlElement* xml, XMPPNamespace::Type t, JBStream::Type sType,
    const char* from, const char* to, const char* operation)
{
    Message* m = __plugin.message("jabber.feature");
    m->addParam("feature",XMPPUtils::s_ns[t]);
    addValidParam(*m,"operation",operation);
    m->addParam("stream_type",lookup(sType,JBStream::s_typeName));
    m->addParam("from",from);
    addValidParam(*m,"to",to);
    if (xml)
	m->addParam(new NamedPointer("xml",xml));
    return m;
}

// Build a xmpp.iq message
Message* YJBEngine::xmppIq(JBEvent* ev, const char* xmlns)
{
    Message* m = __plugin.message("xmpp.iq");
    m->addParam(new NamedPointer("xml",ev->releaseXml()));
    addValidParam(*m,"to",ev->to());
    addValidParam(*m,"from",ev->from());
    addValidParam(*m,"id",ev->id());
    addValidParam(*m,"type",ev->stanzaType());
    addValidParam(*m,"xmlns",xmlns);
    return m;
}

// Build an user.(un)register message
Message* YJBEngine::userRegister(JBStream& stream, bool reg, const char* instance)
{
    Message* m = __plugin.message(reg ? "user.register" : "user.unregister");
    if (stream.type() == JBStream::c2s)
	m->addParam("username",stream.remote().bare());
    else
	m->addParam("server",String::boolText(true));
    JabberID data(stream.remote().node(),stream.remote().domain(),instance);
    m->addParam("data",data);
    if (reg) {
	SocketAddr addr;
	if (stream.remoteAddr(addr)) {
	    m->addParam("ip_host",addr.host());
	    m->addParam("ip_port",String(addr.port()));
	}
    }
    return m;
}

// Fill module status params
void YJBEngine::statusParams(String& str)
{
    lock();
    unsigned int c2s = m_c2sReceive ? m_c2sReceive->streamCount() : 0;
    unsigned int s2s = m_s2sReceive ? m_s2sReceive->streamCount() : 0;
    unsigned int comp = m_compReceive ? m_compReceive->streamCount() : 0;
    unlock();
    str << lookup(JBStream::c2s,JBStream::s_typeName) << "=" << c2s;
    str << "," << lookup(JBStream::s2s,JBStream::s_typeName) << "=" << s2s;
    str << "," << lookup(JBStream::comp,JBStream::s_typeName) << "=" << comp;
}

// Fill module status detail
unsigned int YJBEngine::statusDetail(String& str, JBStream::Type t, JabberID* remote)
{
    XDebug(this,DebugAll,"statusDetail('%s','%s')",
	lookup(t,JBStream::s_typeName),TelEngine::c_safe(remote));
    RefPointer<JBStreamSetList> list[JBStream::TypeCount];
    getStreamLists(list,t);
    str << "format=Direction|Type|Status|Local|Remote";
    unsigned int n = 0;
    for (unsigned int i = 0; i < JBStream::TypeCount; i++) {
	if (!list[i])
	    continue;
	list[i]->lock();
	for (ObjList* o = list[i]->sets().skipNull(); o; o = o->skipNext()) {
	    JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	    for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
		JBStream* stream = static_cast<JBStream*>(s->get());
		Lock lock(stream);
		if (!remote || (i == JBStream::c2s && stream->remote().match(*remote))) {
		    n++;
		    streamDetail(str,stream);
		}
		else if (i == JBStream::s2s) {
		    JBServerStream* s2s = stream->serverStream();
		    if ((s2s->outgoing() && s2s->remote() == *remote) ||
			(s2s->incoming() && s2s->hasRemoteDomain(*remote,false))) {
			n++;
			streamDetail(str,stream);
		    }
		}
	    }
	}
	list[i]->unlock();
	list[i] = 0;
    }
    return n;
}

// Complete stream details
void YJBEngine::statusDetail(String& str, const String& name)
{
    XDebug(this,DebugAll,"statusDetail(%s)",name.c_str());
    JBStream* stream = findStream(name);
    if (!stream)
	return;
    Lock lock(stream);
    str.append("name=",";");
    str << stream->toString();
    str << ",direction=" << (stream->incoming() ? "incoming" : "outgoing");
    str << ",type=" << stream->typeName();
    str << ",state=" << stream->stateName();
    str << ",local=" << stream->local();
    str << ",remote=";
    fillStreamRemote(str,*stream);
    SocketAddr l;
    stream->localAddr(l);
    str << ",localip=" << l.host() << ":" << l.port();
    SocketAddr r;
    stream->remoteAddr(r);
    str << ",remoteip=" << r.host() << ":" << r.port();
    String buf;
    XMPPUtils::buildFlags(buf,stream->flags(),JBStream::s_flagName);
    str << ",flags=" << buf;
}

// Complete stream detail
void YJBEngine::streamDetail(String& str, JBStream* stream)
{
    str << ";" << stream->toString() << "=";
    str << (stream->incoming() ? "incoming" : "outgoing");
    str << "|" << stream->typeName();
    str << "|" << stream->stateName();
    str << "|" << stream->local();
    str << "|";
    fillStreamRemote(str,*stream);
}

// Complete remote party jid starting with partWord
void YJBEngine::completeStreamRemote(String& str, const String& partWord, JBStream::Type t)
{
    lock();
    RefPointer<JBStreamSetList> list = 0;
    if (t == JBStream::c2s)
	list = m_c2sReceive;
    else if (t == JBStream::s2s)
	list = m_s2sReceive;
    unlock();
    if (!list)
	return;
    list->lock();
    for (ObjList* o = list->sets().skipNull(); o; o = o->skipNext()) {
	JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
	    JBStream* stream = static_cast<JBStream*>(s->get());
	    Lock lock(stream);
	    if (t == JBStream::c2s || stream->outgoing())
		Module::itemComplete(str,stream->remote(),partWord);
	    else if (t == JBStream::s2s && stream->incoming()) {
		JBServerStream* s2s = stream->serverStream();
		unsigned int n = s2s->remoteDomains().length();
		for (unsigned int i = 0; i < n; i++) {
		    NamedString* ns = s2s->remoteDomains().getParam(i);
		    if (ns && ns->name())
			Module::itemComplete(str,ns->name(),partWord);
		}
	    }
	}
    }
    list->unlock();
    list = 0;
}

// Complete stream id starting with partWord
void YJBEngine::completeStreamName(String& str, const String& partWord)
{
    lock();
    RefPointer<JBStreamSetList> list[2] = {m_c2sReceive,m_s2sReceive};
    unlock();
    for (unsigned int i = 0; i < 2; i++) {
	if (!list[i])
	    continue;
	list[i]->lock();
	for (ObjList* o = list[i]->sets().skipNull(); o; o = o->skipNext()) {
	    JBStreamSet* set = static_cast<JBStreamSet*>(o->get());
	    for (ObjList* s = set->clients().skipNull(); s; s = s->skipNext()) {
		JBStream* stream = static_cast<JBStream*>(s->get());
		Lock lock(stream);
		if (!partWord || stream->toString().startsWith(partWord))
		    Module::itemComplete(str,stream->toString(),partWord);
	    }
	}
	list[i]->unlock();
	list[i] = 0;
    }
}

// Notify an incoming s2s stream about a dialback verify response
void YJBEngine::notifyDbVerifyResult(const JabberID& local, const JabberID& remote,
    const String& id, XMPPError::Type rsp)
{
    if (!id)
	return;
    // Notify the incoming stream
    JBServerStream* notify = findServerStream(local,remote,false,false);
    if (notify && notify->id() == id)
	notify->sendDbResult(local,remote,rsp);
    else
	Debug(this,DebugNote,
	    "No incoming s2s stream local=%s remote=%s id='%s' to notify dialback verify result",
	    local.c_str(),remote.c_str(),id.c_str());
    TelEngine::destruct(notify);
}

// Add a resource to binding resources list. Make sure the resource is unique
// Return true on success
bool YJBEngine::bindingResource(const JabberID& user)
{
    Lock lock(this);
    if (!user.resource() || restrictedResource(user.resource()) ||
	findBindingRes(user))
	return false;
    Message* m = __plugin.message("resource.notify");
    m->addParam("operation","query");
    m->addParam("nodata",String::boolText(true));
    m->addParam("contact",user.bare());
    m->addParam("instance",user.resource());
    bool ok = !Engine::dispatch(*m);
    TelEngine::destruct(m);
    if (ok)
	m_bindingResources.append(new JabberID(user));
    return ok;
}


/*
 * JBPendingJob
 */
JBPendingJob::JBPendingJob(JBEvent* ev)
    : m_event(ev),
    m_stream(ev->stream()->toString()),
    m_streamType((JBStream::Type)ev->stream()->type()),
    m_local(ev->stream()->local().domain()),
    m_flags(ev->stream()->flags()),
    m_serverTarget(false),
    m_serverItemTarget(false)
{
    m_serverItemTarget = ev->to() && s_jabber->isServerItemDomain(ev->to().domain());
    m_serverTarget = !m_serverItemTarget && (!ev->to() || ev->to() == ev->stream()->local());
    m_event->releaseStream(true);
}

// Retrieve the stream from jabber engine
JBStream* JBPendingJob::getStream()
{
    // Don't use the stream id when finding a s2s stream
    // We can use any stream to a remote domain to send stanzas
    if (m_streamType != JBStream::s2s)
	return s_jabber->findStream(m_stream,m_streamType);
    return s_jabber->getServerStream(m_event->to(),m_event->from());
}

// Retrieve the stream from jabber engine
// Send the given stanza
// The pointer will be consumed and zeroed
void JBPendingJob::sendStanza(XmlElement*& xml, bool regular)
{
    if (!xml)
	return;
    JBStream* stream = getStream();
    XDebug(&__plugin,DebugAll,
	"JBPendingJob event=%s from=%s to=%s sending '%s' stream (%p,%s)",
	m_event->name(),m_event->from().c_str(),m_event->to().c_str(),xml->tag(),
	stream,stream ? stream->toString().c_str() : "");
    if (stream) {
	xml->setAttributeValid("from",m_event->to());
	if (stream->type() != JBStream::c2s)
	    xml->setAttributeValid("to",m_event->from());
	if (regular)
	    stream->sendStanza(xml);
	else {
	    stream->sendStreamXml(stream->state(),xml);
	    xml = 0;
	}
    }
    TelEngine::destruct(xml);
    TelEngine::destruct(stream);
}


/*
 * JBPendingWorker
 */
JBPendingWorker::JBPendingWorker(unsigned int index, Thread::Priority prio)
    : Thread("JBPendingWorker",prio),
    Mutex(true,__plugin.name() + ":JBPendingWorker"),
    m_index((unsigned int)-1)
{
    // NOTE: Don't lock non-reentrant global mutex: the thread is created with this mutex locked
    if (index < s_threadCount) {
	m_index = index;
	s_threads[index] = this;
    }
}

void JBPendingWorker::cleanup()
{
    if (resetIndex())
	Debug(&__plugin,DebugWarn,"JBPendingWorker(%u) abnormally terminated! [%p]",
	    m_index,this);
}

void JBPendingWorker::run()
{
    Debug(&__plugin,DebugAll,"JBPendingWorker(%u) start running [%p]",m_index,this);
    bool processed = false;
    while (true) {
	if (processed)
	    Thread::msleep(2,false);
	else
	    Thread::idle(false);
	if (Thread::check(false))
	    break;
	lock();
	JBPendingJob* job = static_cast<JBPendingJob*>(m_jobs.remove(false));
	unlock();
	processed = job && job->m_event && job->m_event->element();
	if (processed) {
	    switch (XMPPUtils::tag(*job->m_event->element())) {
		case XmlTag::Message:
		    processChat(*job);
		    break;
		case XmlTag::Iq:
		    processIq(*job);
		    break;
		default:
		    DDebug(&__plugin,DebugStub,
			"JBPendingWorker unhandled xml tag '%s' [%p]",
			job->m_event->element()->tag(),this);
	    }
	}
	TelEngine::destruct(job);
    }
    resetIndex();
    Debug(&__plugin,DebugAll,"JBPendingWorker(%u) terminated [%p]",m_index,this);
}

// Initialize (start) the worker threads
void JBPendingWorker::initialize(unsigned int threads, Thread::Priority prio)
{
    Lock lock(s_mutex);
    if (s_threads)
	return;
    s_threads = new JBPendingWorker*[threads];
    s_threadCount = threads;
    DDebug(&__plugin,DebugAll,"JBPendingWorker::initialize(%u,%u)",threads,prio);
    for (unsigned int i = 0; i < s_threadCount; i++) {
	s_threads[i] = 0;
	(new JBPendingWorker(i,prio))->startup();
    }
}

// Cancel worker threads. Wait for them to terminate
void JBPendingWorker::stop()
{
    if (!s_threads)
	return;
    s_mutex.lock();
    unsigned int threads = 0;
    for (unsigned int i = 0; i < s_threadCount; i++) {
	if (s_threads[i]) {
	    threads++;
	    s_threads[i]->cancel();
	}
    }
    s_mutex.unlock();
    if (!threads) {
	delete[] s_threads;
	s_threads = 0;
	return;
    }
    DDebug(&__plugin,DebugAll,"Waiting for %u pending worker threads to terminate",threads);
    bool haveThreads = false;
    while (true) {
	haveThreads = false;
	s_mutex.lock();
	for (unsigned int i = 0; i < s_threadCount; i++)
	    if (s_threads[i]) {
		haveThreads = true;
		break;
	    }
	s_mutex.unlock();
	if (!haveThreads)
	    break;
	Thread::yield();
    }
    Debug(&__plugin,DebugAll,"Terminated %u pending worker threads",threads);
    Lock lock(s_mutex);
    delete[] s_threads;
    s_threads = 0;
}

// Add a job to one of the threads
bool JBPendingWorker::add(JBEvent* ev)
{
    if (!(ev && ev->element() && ev->stream()))
	return false;
    if (Engine::exiting()) {
	ev->sendStanzaError(XMPPError::Shutdown,0,XMPPError::TypeCancel);
	return false;
    }
    if (!ev->ref()) {
	ev->sendStanzaError(XMPPError::Internal);
	return false;
    }
    // TODO: avoid locking the global mutex (the thread's job list may be long)
    // Add a busy flag used to protect the thread list and protected by the global mutex
    Lock lock(s_mutex);
    String id(ev->from());
    if (ev->stream()->type() == JBStream::s2s)
	id << ev->to();
    unsigned int index = id.hash() % s_threadCount;
    JBPendingWorker* th = s_threads[index];
    if (th) {
	Lock lock(th);
	// Don't move the debug after the append(): event will loose its xml element
	XDebug(&__plugin,DebugAll,"JBPendingWorker(%u) added job xml=%s from=%s to=%s [%p]",
	    th->m_index,ev->element()->tag(),ev->from().c_str(),ev->to().c_str(),th);
	th->m_jobs.append(new JBPendingJob(ev));
	return true;
    }
    lock.drop();
    ev->sendStanzaError(XMPPError::Internal);
    TelEngine::destruct(ev);
    return false;
}

// Process chat jobs
void JBPendingWorker::processChat(JBPendingJob& job)
{
    JBEvent* ev = job.m_event;
    Debug(&__plugin,DebugAll,
	"JBPendingWorker(%u) processing (%p,%s) from=%s to=%s [%p]",
	m_index,ev->element(),ev->element()->tag(),ev->from().c_str(),
	ev->to().c_str(),this);
    int mType = XMPPUtils::msgType(ev->stanzaType());
    if (!ev->to()) {
	if (mType != XMPPUtils::MsgError)
	    job.sendChatErrorStanza(XMPPError::ServiceUnavailable);
	return;
    }
    XMPPError::Type error = XMPPError::NoError;
    bool localTarget = s_jabber->hasDomain(ev->to().domain()) &&
	!s_jabber->hasComponent(ev->to().domain()) &&
	!s_jabber->isServerItemDomain(ev->to().domain());

    // NOTE: RFC3921bis recommends to broadcast only 'headline' messages
    //  for bare jid target (or target resource not found)
    //  and send 'chat' and 'normal' to the highest priority resource

    // Process now some stanzas with bare jid target in local domain
    if (localTarget && !ev->to().resource()) {
	// See RFC3921bis 8.3
	// Discard errors
	if (mType == XMPPUtils::MsgError)
	    return;
	// Deny groupchat without resource
	if (mType == XMPPUtils::GroupChat) {
	    if (mType != XMPPUtils::MsgError)
		job.sendChatErrorStanza(XMPPError::ServiceUnavailable);
	    return;
	}
    }

    Message m("msg.route");
    while (true) {
	__plugin.complete(m);
	m.addParam("type",ev->stanzaType());
	addValidParam(m,"id",ev->id());
	m.addParam("caller",ev->from().bare());
	addValidParam(m,"called",ev->to().bare());
	addValidParam(m,"caller_instance",ev->from().resource());
	addValidParam(m,"called_instance",ev->to().resource());
	if (localTarget) {
	    bool ok = Engine::dispatch(m);
	    if (!ok || (m.retValue() == "-") || (m.retValue() == "error")) {
		// Check if an 'instance.count' parameter was returned:
		//  the target exists
		if (m.getParam("instance.count"))
		    error = XMPPError::ItemNotFound;
		else
		    error = XMPPError::ServiceUnavailable;
		break;
	    }
	    // Directed message with instance not found
	    if (ev->to().resource()) {
		if (m.getIntValue("instance.count")) {
		    // Clear instance.count to signal directed message
		    m.clearParam("instance.count");
		}
		else {
		    // See RFC3921bis 8.2.2
		    // Discard errors
		    if (mType == XMPPUtils::MsgError)
			break;
		    // Deny groupchat
		    if (mType == XMPPUtils::GroupChat) {
			error = XMPPError::ServiceUnavailable;
			break;
		    }
		    // Broadcast all other types
		    m.clearParam("called_instance");
		    ok = Engine::dispatch(m);
		    if (!ok || (m.retValue() == "-") || (m.retValue() == "error")) {
			// Check if an 'instance.count' parameter was returned: the target exists
			if (m.getParam("instance.count"))
			    error = XMPPError::ItemNotFound;
			else
			    error = XMPPError::ServiceUnavailable;
			break;
		    }
		    // Add again the called_instance param to signal directed message
		    m.addParam("called_instance",ev->to().resource());
		}
	    }
	}
	// Check route(s)
	m = "msg.execute";
//	m.setParam("callto",m.retValue());
	m.clearParam("error");
	m.retValue().clear();
	XmlElement* xml = ev->releaseXml();
	addValidParam(m,"subject",XMPPUtils::subject(*xml));
	addValidParam(m,"body",XMPPUtils::body(*xml));
	m.addParam(new NamedPointer("xml",xml));
	if (!Engine::dispatch(m))
	   error = XMPPError::Gone;
	break;
    }
    if (error == XMPPError::NoError)
	return;
    if (localTarget && error == XMPPError::ItemNotFound) {
	// Store offline messages addressed to our users
	bool ok = false;
	XmlElement* xml = ev->releaseXml();
	if (!xml)
	    xml = XMPPUtils::getChatXml(m);
	if (xml) {
	    // Save only 'chat' and 'normal' messages
	    if (mType == XMPPUtils::Chat || mType == XMPPUtils::Normal) {
		Message* f = s_jabber->jabberFeature(xml,XMPPNamespace::MsgOffline,
		    job.m_streamType,ev->from(),ev->to());
		f->addParam("time",String(m.msgTime().sec()));
		ok = Engine::dispatch(f);
		TelEngine::destruct(f);
	    }
	}
	if (ok)
	    return;
	error = XMPPError::ServiceUnavailable;
    }
    if (mType != XMPPUtils::MsgError)
	job.sendChatErrorStanza(error);
}

// Process iq jobs
void JBPendingWorker::processIq(JBPendingJob& job)
{
    JBEvent* ev = job.m_event;
    XmlElement* service = ev->child();
    XMPPUtils::IqType t = XMPPUtils::iqType(ev->stanzaType());
    String* xmlns = 0;
    int ns = XMPPNamespace::Count;
    if (service) {
	xmlns = service->xmlns();
	if (xmlns)
	    ns = XMPPUtils::s_ns[*xmlns];
    }
    Debug(&__plugin,DebugAll,
	"JBPendingWorker(%u) processing (%p,%s) type=%s from=%s to=%s child=(%s,%s) stream=%s [%p]",
	m_index,ev->element(),ev->element()->tag(),ev->stanzaType().c_str(),
	ev->from().c_str(),ev->to().c_str(),service ? service->tag() : "",
	TelEngine::c_safe(xmlns),lookup(job.m_streamType,JBStream::s_typeName),this);
    // Server entity caps responses
    if (ns == XMPPNamespace::DiscoInfo &&
	(t == XMPPUtils::IqResult || t == XMPPUtils::IqError) &&
	s_entityCaps.processRsp(ev->element(),ev->id(),t == XMPPUtils::IqResult))
	return;

    XmlElement* rsp = 0;
    // Handle here some stanzas addressed to the server
    if (job.m_serverTarget) {
	// Responses
	if (t != XMPPUtils::IqGet && t != XMPPUtils::IqSet) {
	    // TODO: ?
	    return;
	}
	switch (ns) {
	    // XEP-0030 Service Discovery
	    case XMPPNamespace::DiscoInfo:
		rsp = s_jabber->discoInfo(ev,job.m_streamType);
		break;
	    // Disco items
	    case XMPPNamespace::DiscoItems:
		rsp = s_jabber->discoItems(ev);
		break;
	    // XEP-0092 Software version
	    case XMPPNamespace::IqVersion:
		if (t == XMPPUtils::IqGet &&
		    service->toString() == XMPPUtils::s_tag[XmlTag::Query])
		    rsp = XMPPUtils::createIqVersionRes(0,0,ev->id(),
			s_jabber->m_progName,s_jabber->m_progVersion);
		else
		    rsp = ev->buildIqError(false,XMPPError::ServiceUnavailable);
		break;
	    // RFC 3921 - session establishment
	    // Deprecated in RFC 3921 bis
	    case XMPPNamespace::Session:
		if (job.m_streamType == JBStream::c2s && t == XMPPUtils::IqSet &&
		    service->toString() == XMPPUtils::s_tag[XmlTag::Session])
		    rsp = ev->buildIqResult(false);
		else
		    rsp = ev->buildIqError(false,XMPPError::ServiceUnavailable);
		break;
	    default: ;
	}
    }
    // Respond ?
    if (rsp) {
	job.sendStanza(rsp);
	return;
    }
    // Check some other known namespaces
    switch (ns) {
	// RFC 3921 Roster management
	// Namespace restricted for non c2s streams
	case XMPPNamespace::Roster:
	    if (job.m_serverItemTarget)
		break;
	    rsp = s_jabber->processIqRoster(ev,job.m_streamType,t);
	    if (rsp)
		job.sendStanza(rsp);
	    // Set roster requested flag
	    if (job.m_streamType == JBStream::c2s && t == XMPPUtils::IqGet) {
		JBStream* stream = job.getStream();
		if (stream) {
		    stream->setRosterRequested(true);
		    TelEngine::destruct(stream);
		}
	    }
	    return;
	// XEP-0054 vcard-temp
	case XMPPNamespace::VCard:
	    // vcard requests from remote domain
	    if (job.m_streamType != JBStream::c2s)
		break;
	    // vcard requests to remote domain or to server items
	    if (job.m_serverItemTarget || (ev->to() && !s_jabber->hasDomain(ev->to().domain())))
		break;
	    rsp = s_jabber->processIqVCard(ev,job.m_streamType,t);
	    if (rsp)
		job.sendStanza(rsp);
	    return;
	// XEP-0049 Private XML storage
	// Namespace restricted for non c2s streams
	case XMPPNamespace::IqPrivate:
	    rsp = s_jabber->processIqPrivate(ev,job.m_streamType,t);
	    if (rsp)
		job.sendStanza(rsp);
	    return;
	// XEP-0199 XMPP Ping
	// See Section 4.2 Client to Server ping
	// See Section 4.3 Server to Server ping
	case XMPPNamespace::Ping:
	    if (job.m_serverTarget || (job.m_streamType == JBStream::c2s &&
		ev->to().bare() == ev->from().bare())) {
		if (t == XMPPUtils::IqGet &&
		    service->toString() == XMPPUtils::s_tag[XmlTag::Ping])
		    job.sendIqResultStanza();
		else
		    job.sendIqErrorStanza(XMPPError::ServiceUnavailable);
		return;
	    }
	    break;
	// XEP-0077 In-Band Registration
	// Namespace restricted for non c2s streams
	case XMPPNamespace::IqRegister:
	    if (job.m_serverTarget) {
		rsp = s_jabber->processIqRegister(ev,job.m_streamType,t,
		    job.m_local,job.m_flags);
		job.sendStanza(rsp,false);
	    }
	    else
		job.sendIqErrorStanza(XMPPError::ServiceUnavailable);
	    return;
	// XEP-0078 Non SASL authentication
	case XMPPNamespace::IqAuth:
	    if (job.m_serverTarget) {
		rsp = s_jabber->processIqAuth(ev,job.m_streamType,t,job.m_flags);
		job.sendStanza(rsp,false);
	    }
	    else
		job.sendIqErrorStanza(XMPPError::ServiceUnavailable);
	    return;
	default: ;
    }

    bool respond = (t == XMPPUtils::IqGet || t == XMPPUtils::IqSet);
    // Route the iq
    Message m("jabber.iq");
    m.addParam("module",__plugin.name());
    m.addParam("from",ev->from().bare());
    m.addParam("from_instance",ev->from().resource());
    m.addParam("to",ev->to().bare());
    m.addParam("to_instance",ev->to().resource());
    addValidParam(m,"id",ev->id());
    addValidParam(m,"type",ev->stanzaType());
    if (respond)
	addValidParam(m,"xmlns",TelEngine::c_safe(xmlns));
    XmlElement* iq = ev->releaseXml();
    if (s_dumpIq) {
	NamedString* ns = new NamedString("data");
	iq->toString(*ns);
	m.addParam(ns);
    }
    m.addParam(new NamedPointer("xml",iq));
    if (Engine::dispatch(m)) {
	if (respond) {
	    XmlElement* xml = XMPPUtils::getXml(m,"response",0);
	    if (xml)
		job.sendStanza(xml);
	    else if (m.getBoolValue("respond"))
		job.sendIqResultStanza();
	}
	return;
    }
    if (respond)
	job.sendIqErrorStanza(XMPPError::ServiceUnavailable);
}

// Reset the global index
bool JBPendingWorker::resetIndex()
{
    Lock lock(s_mutex);
    DDebug(&__plugin,DebugAll,"JBPendingWorker(%u) resetting global list entry [%p]",
	m_index,this);
    if (s_threads && m_index < s_threadCount && s_threads[m_index]) {
	s_threads[m_index] = 0;
	return true;
    }
    return false;
}


/*
 * UserAuthMessage
 */
// Fill the message with authentication data
UserAuthMessage::UserAuthMessage(JBEvent* ev)
    : Message("user.auth"),
    m_stream(ev->stream()->toString()), m_streamType((JBStream::Type)ev->stream()->type())
{
    XDebug(&__plugin,DebugAll,"UserAuthMessage stream=%s type=%u [%p]",
	m_stream.c_str(),m_streamType,this);
    __plugin.complete(*this);
    addParam("streamtype",ev->stream()->typeName());
    SocketAddr addr;
    if (ev->stream()->remoteAddr(addr)) {
	addParam("ip_host",addr.host());
	addParam("ip_port",String(addr.port()));
    }
    addParam("requestid",ev->id());
}

UserAuthMessage::~UserAuthMessage()
{
    if (m_bindingUser)
	s_jabber->removeBindingResource(m_bindingUser);
}

// Check accepted and returned value. Calls stream's authenticated() method
void UserAuthMessage::dispatched(bool accepted)
{
    JBStream* stream = s_jabber->findStream(m_stream,m_streamType);
    XDebug(&__plugin,DebugAll,"UserAuthMessage::dispatch(%u) stream=(%p,%s) type=%u",
	accepted,stream,m_stream.c_str(),m_streamType);
    bool ok = false;
    String rspValue;
    JabberID username = getValue("username");
    // Use a while() to break to the end
    while (stream) {
	Lock lock(stream);
	// Returned value '-' means deny
	if (accepted && retValue() == "-")
	    break;
	// Empty password returned means authentication succeeded
	if (!(accepted || retValue()))
	    break;
	// Returned password works only with username
	if (!username)
	    break;
	// Check credentials
	if (m_streamType == JBStream::c2s) {
	    if (stream->m_sasl) {
		XDebug(&__plugin,DebugAll,"UserAuthMessage checking c2s sasl [%p]",this);
		String* rsp = getParam("response");
		if (rsp) {
		    if (stream->m_sasl->m_plain)
			ok = (*rsp == retValue());
		    else {
			String digest;
			stream->m_sasl->buildMD5Digest(digest,retValue(),true);
			ok = (*rsp == digest);
			if (ok)
			    stream->m_sasl->buildMD5Digest(rspValue,retValue(),false);
		    }
		}
	    }
	    else {
		XDebug(&__plugin,DebugAll,"UserAuthMessage checking c2s non-sasl [%p]",this);
		String* auth = getParam("digest");
		if (auth) {
		    String digest;
		    stream->buildSha1Digest(digest,retValue());
		    ok = (digest == *auth);
		}
		else {
		    auth = getParam("password");
		    ok = auth && (*auth == retValue());
		}
	    }
	}
	else if (stream->type() == JBStream::comp) {
	    XDebug(&__plugin,DebugAll,"UserAuthMessage checking component handshake [%p]",this);
	    String digest;
	    stream->buildSha1Digest(digest,retValue());
	    ok = (digest == getValue("handshake"));
	}
	break;
    }
    if (stream)
	stream->authenticated(ok,rspValue,XMPPError::NotAuthorized,username.node(),
	    getValue("requestid"),getValue("instance"));
    TelEngine::destruct(stream);
}


/*
 * JBMessageHandler
 */
JBMessageHandler::JBMessageHandler(int handler)
    : MessageHandler(lookup(handler,s_msgHandler),handler < 0 ? 100 : handler),
    m_handler(handler)
{
}

bool JBMessageHandler::received(Message& msg)
{
    switch (m_handler) {
	case JabberIq:
	    return s_jabber->handleJabberIq(msg);
	case ResNotify:
	    return s_jabber->handleResNotify(msg);
	case ResSubscribe:
	    return s_jabber->handleResSubscribe(msg);
	case UserRoster:
	    if (!__plugin.isModule(msg))
		s_jabber->handleUserRoster(msg);
	    return false;
	case UserUpdate:
	    if (!__plugin.isModule(msg))
		s_jabber->handleUserUpdate(msg);
	    return false;
	case JabberItem:
	    return s_jabber->handleJabberItem(msg);
	default:
	    DDebug(&__plugin,DebugStub,"JBMessageHandler(%s) not handled!",msg.c_str());
    }
    return false;
}


/*
 * TcpListener
 */
// Remove from plugin
TcpListener::~TcpListener()
{
    if (m_socket.valid() && !Engine::exiting())
	Debug(&__plugin,DebugWarn,"Listener(%s) '%s:%d' abnormally terminated [%p]",
	    c_str(),m_address.safe(),m_port,this);
    terminateSocket();
    __plugin.listener(this,false);
}

// Objects added to socket.ssl message when incoming connection is using SSL
class RefSocket : public RefObject
{
public:
    inline RefSocket(Socket** sock)
	: m_socket(sock)
	{}
    virtual void* getObject(const String& name) const {
	    if (name == "Socket*")
		return (void*)m_socket;
	    return RefObject::getObject(name);
	}
    Socket** m_socket;
private:
    RefSocket() {}
};

// Bind and listen
void TcpListener::run()
{
    __plugin.listener(this,true);
    Debug(&__plugin,DebugInfo,
	"Listener(%s) '%s:%d' type='%s' context=%s start running [%p]",
	c_str(),m_address.safe(),m_port,lookup(m_type,JBStream::s_typeName),
	m_sslContext.c_str(),this);
    // Create the socket
    if (!m_socket.create(PF_INET,SOCK_STREAM)) {
	terminateSocket("failed to create socket");
	return;
    }
    m_socket.setReuse();
    // Bind the socket
    SocketAddr addr(PF_INET);
    addr.host(m_address);
    addr.port(m_port);
    if (!m_socket.bind(addr)) {
	terminateSocket("failed to bind");
	return;
    }
    m_socket.setBlocking(false);
    // Start listening
    if (!m_socket.listen(m_backlog)) {
	terminateSocket("failed to start listening");
	return;
    }
    XDebug(&__plugin,DebugAll,"Listener(%s) '%s:%d' start listening [%p]",
	c_str(),m_address.safe(),m_port,this);
    bool plain = m_sslContext.null();
    while (true) {
	if (Thread::check(false))
	    break;
	SocketAddr addr(PF_INET);
	Socket* sock = m_socket.accept(addr);
	bool processed = false;
	if (sock) {
	    DDebug(&__plugin,DebugAll,"Listener(%s) '%s:%d' got conn from '%s:%d' [%p]",
		c_str(),m_address.safe(),m_port,addr.host().c_str(),addr.port(),this);
	    if (plain)
		processed = m_engine && m_engine->acceptConn(sock,addr,m_type);
	    else {
		Message m("socket.ssl");
		m.userData(new RefSocket(&sock));
		m.addParam("server",String::boolText(true));
		m.addParam("context",m_sslContext);
		if (Engine::dispatch(m))
		    processed = m_engine && m_engine->acceptConn(sock,addr,m_type,true);
		else {
		    Debug(&__plugin,DebugWarn,"Listener(%s) Failed to start SSL [%p]",
			c_str(),this);
		    delete sock;
		    break;
		}
	    }
	    if (!processed)
		delete sock;
	}
	Thread::idle();
    }
    terminateSocket();
    Debug(&__plugin,DebugInfo,"Listener(%s) '%s:%d' terminated [%p]",c_str(),
	m_address.safe(),m_port,this);
    __plugin.listener(this,false);
}

// Terminate the socket. Show an error if context is not null
void TcpListener::terminateSocket(const char* context)
{
    if (context) {
	String s;
	Thread::errorString(s,m_socket.error());
	Debug(&__plugin,DebugWarn,"Listener(%s) '%s:%d' %s. %d: '%s' [%p]",
	    c_str(),m_address.safe(),m_port,context,m_socket.error(),s.c_str(),this);
    }
    m_socket.setLinger(-1);
    m_socket.terminate();
}


/*
 * JBModule
 */
// Early load, late unload: we own the jabber engine
JBModule::JBModule()
    : Module("jabber","misc",true),
    m_init(false)
{
    Output("Loaded module Jabber Server");
}

JBModule::~JBModule()
{
    Output("Unloading module Jabber Server");
    TelEngine::destruct(s_jabber);
}

void JBModule::initialize()
{
    Output("Initializing module Jabber Server");
    Configuration cfg(Engine::configFile("jabberserver"));

    if (!m_init) {
	m_init = true;
	setup();
	installRelay(Halt);
	installRelay(Help);
	installRelay(ImExecute);
	s_jabber = new YJBEngine;
	s_jabber->debugChain(this);
	// Install handlers
	for (const TokenDict* d = s_msgHandler; d->token; d++) {
	    JBMessageHandler* h = new JBMessageHandler(d->value);
	    Engine::install(h);
	    m_handlers.append(h);
	}
	// Start pending job workers
	int n = cfg.getIntValue("general","workers",1);
	if (n < 1)
	    n = 1;
	else if (n > 10)
	    n = 10;
	JBPendingWorker::initialize(n,Thread::priority(cfg.getValue("general","worker_priority")));

	// Load entity caps file
	s_entityCaps.m_enable = cfg.getBoolValue("general","entitycaps",true);
	if (s_entityCaps.m_enable)
	    s_entityCaps.load();
	else
	    Debug(this,DebugAll,"Entity capability is disabled");
    }

    // (re)init globals
    s_s2sFeatures = cfg.getBoolValue("general","s2s_offerfeatures",true);
    s_dumpIq = cfg.getBoolValue("general","dump_iq");

    // Init the engine
    s_jabber->initialize(cfg.getSection("general"),!m_init);

    // Listeners
    unsigned int n = cfg.length();
    for (unsigned int i = 0; i < n; i++) {
	NamedList* p = cfg.getSection(i);
	if (!p)
	    continue;
	String name = *p;
	name.trimBlanks();
	if (!(name.startSkip("listener ",false) && name))
	    continue;
	if (p->getBoolValue("enable"))
	    buildListener(name,*p);
	else
	    cancelListener(name);
    }
}

// Cancel a given listener or all listeners if name is empty
void JBModule::cancelListener(const String& name)
{
    Lock lck(this);
    if (!name) {
	ObjList* o = m_streamListeners.skipNull();
	if (!o)
	    return;
	Debug(this,DebugInfo,"Cancelling %d listener(s)",m_streamListeners.count());
	for (; o; o =  o->skipNext()) {
	    TcpListener* tmp = static_cast<TcpListener*>(o->get());
	    tmp->cancel(false);
	}
    }
    else {
	ObjList* o = m_streamListeners.find(name);
	if (!o)
	    return;
	Debug(this,DebugInfo,"Cancelling listener='%s'",name.c_str());
	(static_cast<TcpListener*>(o->get()))->cancel(false);
    }
    lck.drop();
    while (true) {
	ObjList* tmp = 0;
	lock();
	if (!name)
	    tmp = m_streamListeners.skipNull();
	else
	    tmp = m_streamListeners.find(name);
	unlock();
	if (!tmp)
	    break;
	Thread::yield(true);
    }
    if (!name)
	Debug(this,DebugInfo,"All listeners terminated");
    else
	Debug(this,DebugInfo,"Listener '%s' terminated",name.c_str());
}

// Message handler
bool JBModule::received(Message& msg, int id)
{
    if (id == ImExecute)
	return s_jabber->handleMsgExecute(msg);
    if (id == Status) {
	String target = msg.getValue("module");
	// Target is the module
	if (!target || target == name())
	    return Module::received(msg,id);
	// Check additional commands
	if (!target.startSkip(name(),false))
	    return false;
	target.trimBlanks();
	if (!target)
	    return Module::received(msg,id);
	// Handle: status jabber {stream_name|{c2s|s2s} [remote_jid]}
	String tmp;
	if (!getWord(target,tmp))
	    return false;
	JBStream::Type t = JBStream::lookupType(tmp);
	if (t == JBStream::TypeCount) {
	    statusModule(msg.retValue());
	    s_jabber->statusDetail(msg.retValue(),tmp);
	    msg.retValue() << "\r\n";
	    return true;
	}
	JabberID jid;
	if (target) {
	    if (!getWord(target,tmp))
		return false;
	    jid.set(tmp);
	    if (!jid.valid())
		return false;
	}
	String buf;
	unsigned int n = s_jabber->statusDetail(buf,t,jid ? &jid : 0);
	statusModule(msg.retValue());
	msg.retValue() << ";count=" << n;
	if (n)
	    msg.retValue() << ";" << buf.c_str();
	msg.retValue() << "\r\n";
	return true;
    }
    if (id == Help) {
	String line = msg.getValue("line");
	if (line.null()) {
	    msg.retValue() << s_cmdStatus << "\r\n";
	    msg.retValue() << s_cmdDropAll << "\r\n";
	    msg.retValue() << s_cmdCreate << "\r\n";
	    msg.retValue() << s_cmdDebug << "\r\n";
	    return false;
	}
	if (line != name())
	    return false;
	msg.retValue() << s_cmdStatus << "\r\n";
	msg.retValue() << "Show stream status by type and remote jid or stream name\r\n";
	msg.retValue() << s_cmdDropStreamName << "\r\n";
	msg.retValue() << "Terminate a stream by its name\r\n";
	msg.retValue() << s_cmdDropStream << "\r\n";
	msg.retValue() << "Terminate all streams. Optionally terminate only streams of given type and jid\r\n";
	msg.retValue() << s_cmdCreate << "\r\n";
	msg.retValue() << "Create a server to server stream to a remote domain.\r\n";
	msg.retValue() << s_cmdDebug << "\r\n";
	msg.retValue() << "Show or set the debug level for a stream.\r\n";
	return true;
    }
    if (id == Halt) {
	s_jabber->setExiting();
	// Stop pending job workers
	JBPendingWorker::stop();
	// Uninstall message handlers
	for (ObjList* o = m_handlers.skipNull(); o; o = o->skipNext()) {
	    JBMessageHandler* h = static_cast<JBMessageHandler*>(o->get());
	    Engine::uninstall(h);
	}
	cancelListener();
	s_jabber->cleanup();
	DDebug(this,DebugAll,"Halted");
	return Module::received(msg,id);
    }
    if (id == Timer)
	s_entityCaps.expire(msg.msgTime().msec());
    return Module::received(msg,id);
}

// Fill module status params
void JBModule::statusParams(String& str)
{
    s_jabber->statusParams(str);
}

// Fill module status detail
void JBModule::statusDetail(String& str)
{
    s_jabber->statusDetail(str);
}

// Handle command complete requests
bool JBModule::commandComplete(Message& msg, const String& partLine,
    const String& partWord)
{
    if (partLine.null() && partWord.null())
	return false;
    XDebug(this,DebugAll,"commandComplete() partLine='%s' partWord=%s",
	partLine.c_str(),partWord.c_str());

    // No line or 'help': complete module name
    if (partLine.null() || partLine == "help")
	return Module::itemComplete(msg.retValue(),name(),partWord);
    // Line is module name: complete module commands
    if (partLine == name()) {
	for (const String* list = s_cmds; list->length(); list++)
	    Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }

    String line = partLine;
    String word;
    getWord(line,word);
    if (word == name()) {
	// Line is module name: complete module commands and parameters
	getWord(line,word);
	// Check for a known command
	for (const String* list = s_cmds; list->length(); list++) {
	    if (*list != word)
		continue;
	    if (*list == "drop") {
		// Handle: jabber drop {stream_name|{c2s|s2s|*|all} [remote_jid]}
		getWord(line,word);
		if (line)
		    return true;
		JBStream::Type t = JBStream::lookupType(word);
		if (t != JBStream::TypeCount || word == "all" || word == "*")
		    s_jabber->completeStreamRemote(msg.retValue(),partWord,t);
		else {
		    completeStreamType(msg.retValue(),partWord,true);
		    s_jabber->completeStreamName(msg.retValue(),partWord);
		}
	    }
	    if (*list == "debug") {
		// Handle: jabber debug stream_name [debug_level]
		if (line)
		    return true;
		s_jabber->completeStreamName(msg.retValue(),partWord);
	    }
	    return true;
	}
	// Complete module commands
	for (const String* list = s_cmds; list->length(); list++)
	    Module::itemComplete(msg.retValue(),*list,partWord);
	return true;
    }
    if (word == "status") {
	// Handle: status jabber [stream_name|{c2s|s2s} [remote_jid]]";
	getWord(line,word);
	if (word != name())
	    return Module::commandComplete(msg,partLine,partWord);
	getWord(line,word);
	if (word) {
	    if (line)
		return false;
	    JBStream::Type t = JBStream::lookupType(word);
	    if (t != JBStream::TypeCount)
		s_jabber->completeStreamRemote(msg.retValue(),partWord,t);
	    else {
		completeStreamType(msg.retValue(),partWord);
		s_jabber->completeStreamName(msg.retValue(),partWord);
	    }
	}
	else {
	    // Complete stream type/name
	    completeStreamType(msg.retValue(),partWord);
	    s_jabber->completeStreamName(msg.retValue(),partWord);
	}
	return true;
    }
    return Module::commandComplete(msg,partLine,partWord);
}

// Handle command request
bool JBModule::commandExecute(String& retVal, const String& line)
{
    String l = line;
    String word;
    getWord(l,word);
    if (word != name())
	return false;
    getWord(l,word);
    DDebug(this,DebugAll,"Executing command '%s' params '%s'",word.c_str(),l.c_str());
    if (word == "drop") {
	Debug(this,DebugAll,"Executing '%s' command line=%s",word.c_str(),line.c_str());
	getWord(l,word);
	JBStream::Type t = JBStream::lookupType(word);
	if (t != JBStream::TypeCount || word == "all" || word == "*") {
	    // Handle: jabber drop {c2s|s2s|*|all} [remote_jid]"
	    JabberID remote(l);
	    unsigned int n = 0;
	    if (remote.valid())
		n = s_jabber->dropAll(t,JabberID::empty(),remote);
	    retVal << "Dropped " << n << " stream(s)";
	}
	else {
	    // Handle: jabber drop stream_name
	    String n(word);
	    n.append(l," ");
	    JBStream* stream = s_jabber->findStream(word);
	    if (stream) {
		stream->terminate(-1,true,0,XMPPError::NoError);
		TelEngine::destruct(stream);
		retVal << "Dropped stream '" << n << "'";
	    }
	    else
		retVal << "Stream '" << n << "' not found";
	}
    }
    else if (word == "create") {
	// Handle s2s stream start
	String remote;
	getWord(l,remote);
	String local;
	getWord(l,local);
	bool hasLocal = true;
	if (!local)
	    s_jabber->firstDomain(local);
	else
	    hasLocal = s_jabber->hasDomain(local);
	bool hasRemote = s_jabber->hasDomain(remote);
	Debug(this,DebugAll,"Executing '%s' command local=%s remote=%s",
	     word.c_str(),local.c_str(),remote.c_str());
	if (remote && !hasRemote && local && hasLocal) {
	    JBStream* s = s_jabber->getServerStream(local.c_str(),remote.c_str());
	    retVal << (s ? "Success" : "Failure");
	    TelEngine::destruct(s);
	}
	else if (!remote || hasRemote)
	    retVal << "Invalid remote domain";
	else
	    retVal << "Invalid local domain";
    }
    else if (word == "debug") {
	Debug(this,DebugAll,"Executing '%s' command line=%s",word.c_str(),line.c_str());
	getWord(l,word);
	JBStream* stream = s_jabber->findStream(word);
	if (stream) {
	    retVal << "Stream '" << word << "' debug";
	    if (l) {
		int level = l.toInteger(-1);
		if (level >= 0) {
		    stream->debugLevel(level);
		    retVal << " at level " << stream->debugLevel();
		}
		else if (l.isBoolean()) {
		    stream->debugEnabled(l.toBoolean());
		    retVal << " is " << (stream->debugEnabled() ? "on" : "off");
		}
	    }
	    else
		 retVal << " at level " << stream->debugLevel();
	    TelEngine::destruct(stream);
	}
	else
	    retVal << "Stream '" << word << "' not found";
    }
    else
	return false;
    retVal << "\r\n";
    return true;
}

// Build a listener from a list of parameters. Add it to the list and start it
bool JBModule::buildListener(const String& name, NamedList& p)
{
    if (!name)
	return false;
    Lock lock(this);
    if (m_streamListeners.find(name))
	return true;
    lock.drop();
    const char* stype = p.getValue("type");
    JBStream::Type t = JBStream::lookupType(stype);
    if (t == JBStream::TypeCount) {
	Debug(this,DebugNote,"Can't build listener='%s' with invalid type='%s'",
	    name.c_str(),stype);
	return false;
    }
    const char* context = 0;
    String* sport = p.getParam("port");
    int port = 0;
    if (!TelEngine::null(sport))
	port = sport->toInteger();
    else if (t == JBStream::s2s)
	port = XMPP_S2S_PORT;
    if (t == JBStream::c2s) {
	context = p.getValue("sslcontext");
	if (TelEngine::null(sport) && TelEngine::null(context))
	    port = XMPP_C2S_PORT;
    }
    if (!port) {
	Debug(this,DebugNote,"Can't build listener='%s' with invalid port='%s'",
	    name.c_str(),c_safe(sport));
	return false;
    }
    const char* addr = p.getValue("address");
    unsigned int backlog = p.getIntValue("backlog",5);
    TcpListener* l = 0;
    if (TelEngine::null(context))
	l = new TcpListener(name,s_jabber,t,addr,port,backlog);
    else
	l = new TcpListener(name,s_jabber,context,addr,port,backlog);
    if (l->startup())
	return true;
    Debug(this,DebugNote,"Failed to start listener='%s' type='%s' addr='%s' port=%d",
	name.c_str(),stype,p.getValue("address"),port);
    TelEngine::destruct(l);
    return false;
}

// Add or remove a listener to/from list
void JBModule::listener(TcpListener* l, bool add)
{
    if (!l)
	return;
    Lock lock(this);
    ObjList* found = m_streamListeners.find(l);
    if (add == (found != 0))
	return;
    if (add)
	m_streamListeners.append(l)->setDelete(false);
    else
	found->remove(false);
    DDebug(this,DebugAll,"%s listener (%p,'%s')",add ? "Added" : "Removed",
	l,l->toString().c_str());
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
