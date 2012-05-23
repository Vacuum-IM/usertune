#ifndef QT_NO_DEBUG
#  include <QDebug>
#endif

#include <definitions/notificationtypes.h>
#include <definitions/notificationdataroles.h>
#include <definitions/notificationtypeorders.h>
#include <definitions/menuicons.h>
#include <definitions/resources.h>
#include <definitions/rosterlabelorders.h>
#include <definitions/rostertooltiporders.h>
#include <definitions/rosterindextyperole.h>

#include <definitions/optionvalues.h>

#include "usertune.h"
#include "mprisfetcher1.h"
#include "mprisfetcher2.h"
#ifdef Q_WS_X11
#include "usertuneoptions.h"
#endif

#include "definitions.h"

#ifdef Q_WS_X11
#define ADD_CHILD_ELEMENT(document, root_element, child_name, child_data) \
{ \
    QDomElement tag = (document).createElement(child_name); \
    QDomText text = (document).createTextNode(child_data); \
    tag.appendChild(text); \
    (root_element).appendChild(tag); \
}
#endif

#define TUNE_PROTOCOL_URL "http://jabber.org/protocol/tune"
#define TUNE_NOTIFY_PROTOCOL_URL "http://jabber.org/protocol/tune+notify"
#define PEP_SEND_DELAY 2*1000 // delay befo send pep to prevent a large number of updates when a user is skipping through tracks

UserTuneHandler::UserTuneHandler() :
    FPEPManager(NULL),
    FServiceDiscovery(NULL),
    FXmppStreams(NULL),
    FOptionsManager(NULL)
#ifdef Q_WS_X11
    , FMetaDataFetcher(NULL)
#endif
{
#ifdef Q_WS_X11
    FTimer.setSingleShot(true);
    FTimer.setInterval(PEP_SEND_DELAY);
    connect(&FTimer, SIGNAL(timeout()), this, SLOT(onSendPep()));
#endif
}

UserTuneHandler::~UserTuneHandler()
{

}

void UserTuneHandler::pluginInfo(IPluginInfo *APluginInfo)
{
    APluginInfo->name = tr("User Tune Handler");
    APluginInfo->description = tr("Allows hadle user tunes");
    APluginInfo->version = "0.9.6";
    APluginInfo->author = "Crying Angel";
    APluginInfo->homePage = "http://www.vacuum-im.org";
    APluginInfo->dependences.append(PEPMANAGER_UUID);
    APluginInfo->dependences.append(SERVICEDISCOVERY_UUID);
    APluginInfo->dependences.append(XMPPSTREAMS_UUID);
}

bool UserTuneHandler::initConnections(IPluginManager *APluginManager, int &AInitOrder)
{
    AInitOrder=500;

    IPlugin *plugin;

    plugin = APluginManager->pluginInterface("IPEPManager").value(0,NULL);
    if (!plugin) return false;

    FPEPManager = qobject_cast<IPEPManager *>(plugin->instance());

    plugin = APluginManager->pluginInterface("IServiceDiscovery").value(0,NULL);
    if (!plugin) return false;
    FServiceDiscovery = qobject_cast<IServiceDiscovery *>(plugin->instance());

    plugin = APluginManager->pluginInterface("IXmppStreams").value(0,NULL);
    if (!plugin) return false;

    FXmppStreams = qobject_cast<IXmppStreams *>(plugin->instance());
    connect(FXmppStreams->instance(), SIGNAL(opened(IXmppStream *)), this, SLOT(onSetMainLabel(IXmppStream*)));
    connect(FXmppStreams->instance(), SIGNAL(closed(IXmppStream *)), this, SLOT(onUnsetMainLabel(IXmppStream*)));

    int streams_size = FXmppStreams->xmppStreams().size();
    for (int i = 0; i < streams_size; i++)
    {
        connect(FXmppStreams->xmppStreams().at(i)->instance(), SIGNAL(aboutToClose()), this, SLOT(onStopPublishing()));
    }

    plugin = APluginManager->pluginInterface("IRosterPlugin").value(0,NULL);
    if (plugin)
    {
        FRosterPlugin = qobject_cast<IRosterPlugin *>(plugin->instance());
    }

    plugin = APluginManager->pluginInterface("IRostersModel").value(0,NULL);
    if (plugin)
    {
        FRostersModel = qobject_cast<IRostersModel *>(plugin->instance());
    }

    plugin = APluginManager->pluginInterface("IRostersViewPlugin").value(0,NULL);
    if (plugin)
    {
        FRostersViewPlugin = qobject_cast<IRostersViewPlugin *>(plugin->instance());
        if (FRostersViewPlugin)
        {
            connect(FRostersViewPlugin->rostersView()->instance(),SIGNAL(indexToolTips(IRosterIndex *, int, QMultiMap<int,QString> &)),
                    SLOT(onRosterIndexToolTips(IRosterIndex *, int, QMultiMap<int,QString> &)));
        }
    }

    plugin = APluginManager->pluginInterface("INotifications").value(0,NULL);
    if (plugin)
    {
        FNotifications = qobject_cast<INotifications *>(plugin->instance());
        if (FNotifications)
        {
            connect(FNotifications->instance(),SIGNAL(notificationActivated(int)), SLOT(onNotificationActivated(int)));
            connect(FNotifications->instance(),SIGNAL(notificationRemoved(int)), SLOT(onNotificationRemoved(int)));
        }
    }

    plugin = APluginManager->pluginInterface("IOptionsManager").value(0,NULL);
    if (plugin)
    {
        FOptionsManager = qobject_cast<IOptionsManager *>(plugin->instance());
    }

    connect(Options::instance(),SIGNAL(optionsOpened()),SLOT(onOptionsOpened()));
    connect(Options::instance(),SIGNAL(optionsChanged(const OptionsNode &)),SLOT(onOptionsChanged(const OptionsNode &)));

    connect (APluginManager->instance(), SIGNAL(aboutToQuit()), this, SLOT(onApplicationQuit()));

    return true;
}

bool UserTuneHandler::initObjects()
{
    handlerId = FPEPManager->insertNodeHandler(TUNE_PROTOCOL_URL, this);

    IDiscoFeature feature;
    feature.active = true;
    feature.name = tr("User tune");
    feature.var = TUNE_PROTOCOL_URL;

    FServiceDiscovery->insertDiscoFeature(feature);

    feature.name = tr("User tune notification");
    feature.var = TUNE_NOTIFY_PROTOCOL_URL;
    FServiceDiscovery->insertDiscoFeature(feature);

    if (FNotifications)
    {
        INotificationType notifyType;
        notifyType.order = NTO_USERTUNE_NOTIFY;
        notifyType.icon = IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->getIcon(MNI_USERTUNE_MUSIC);
        notifyType.title = tr("When reminding of contact playing music");
        notifyType.kindMask = INotification::PopupWindow;
        notifyType.kindDefs = notifyType.kindMask;
        FNotifications->registerNotificationType(NNT_USERTUNE,notifyType);
    }

    if (FRostersViewPlugin)
    {
        IRostersLabel label;
        label.order = RLO_USERTUNE;
        label.value = IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->getIcon(MNI_USERTUNE_MUSIC);
        FUserTuneLabelId = FRostersViewPlugin->rostersView()->registerLabel(label);
    }

    return true;
}

bool UserTuneHandler::initSettings()
{
    Options::setDefaultValue(OPV_UT_SHOW_ROSTER_LABEL,false);
    Options::setDefaultValue(OPV_UT_TAG_FORMAT,"%T - %A - %S");
#ifdef Q_WS_X11
    Options::setDefaultValue(OPV_UT_PLAYER_NAME,"amarok");
    Options::setDefaultValue(OPV_UT_PLAYER_VER,mprisV1);
#elif Q_WS_WIN
    // TODO: сделать для windows
    Options::setDefaultValue(OPV_UT_PLAYER_NAME,"");
    Options::setDefaultValue(OPV_UT_PLAYER_VER,"");
#endif

    if (FOptionsManager)
    {
        IOptionsDialogNode dnode = { ONO_USERTUNE, OPN_USERTUNE, tr("User Tune"), MNI_USERTUNE_MUSIC };
        FOptionsManager->insertOptionsDialogNode(dnode);
        FOptionsManager->insertOptionsHolder(this);
    }

    return true;
}

QMultiMap<int, IOptionsWidget *> UserTuneHandler::optionsWidgets(const QString &ANodeId, QWidget *AParent)
{
    QMultiMap<int, IOptionsWidget *> widgets;
    if (FOptionsManager && ANodeId==OPN_USERTUNE)
    {
#ifdef Q_WS_X11
        widgets.insertMulti(OWO_USERTUNE, new UserTuneOptions(AParent));
#elif Q_WS_WIN
        widgets.insertMulti(OWO_USERTUNE, FOptionsManager->optionsNodeWidget(Options::node(OPV_UT_SHOW_ROSTER_LABEL),tr("Show music icon in roster"),AParent));
        widgets.insertMulti(OWO_USERTUNE, FOptionsManager->optionsNodeWidget(Options::node(OPV_UT_TAG_FORMAT),tr("Tag format:"),AParent));
        widgets.insertMulti(OWO_USERTUNE, FOptionsManager->optionsNodeWidget(Options::node(OPV_UT_PLAYER_NAME),tr("Player name:"),AParent));
#endif
    }
    return widgets;
}

void UserTuneHandler::onOptionsOpened()
{
    onOptionsChanged(Options::node(OPV_UT_SHOW_ROSTER_LABEL));
    onOptionsChanged(Options::node(OPV_UT_TAG_FORMAT));
#ifdef Q_WS_X11
    updateFetchers();
#endif
}

void UserTuneHandler::onOptionsChanged(const OptionsNode &ANode)
{
    if (ANode.path() == OPV_UT_SHOW_ROSTER_LABEL)
    {
        if (Options::node(OPV_UT_SHOW_ROSTER_LABEL).value().toBool())
        {
            setContactLabel();
        }
        else
        {
            unsetContactLabel();
        }
    }
    else if (ANode.path() == OPV_UT_TAG_FORMAT)
    {
        FFormatTag = Options::node(OPV_UT_TAG_FORMAT).value().toString();
    }
#ifdef Q_WS_X11
    else if (ANode.path() == OPV_UT_PLAYER_NAME)
    {
        FMetaDataFetcher->onPlayerNameChange(Options::node(OPV_UT_PLAYER_NAME).value().toString());
    }
    else if (ANode.path() == OPV_UT_PLAYER_VER)
    {
        updateFetchers();
    }
#endif
}

void UserTuneHandler::onShowNotification(const Jid &AStreamJid, const Jid &AContactJid)
{
    if (FNotifications && FNotifications->notifications().isEmpty() && FContactTune.contains(AContactJid))
    {
        INotification notify;
        notify.kinds = FNotifications->enabledTypeNotificationKinds(NNT_USERTUNE);
        if ((notify.kinds & INotification::PopupWindow) > 0)
        {
            notify.typeId = NNT_USERTUNE;
            notify.data.insert(NDR_ICON,IconStorage::staticStorage(RSR_STORAGE_MENUICONS)->getIcon(MNI_USERTUNE_MUSIC));
            notify.data.insert(NDR_POPUP_CAPTION,tr("User Tune Notification"));
            notify.data.insert(NDR_POPUP_TITLE,FNotifications->contactName(AStreamJid, AContactJid));
            notify.data.insert(NDR_POPUP_IMAGE,FNotifications->contactAvatar(AContactJid));

            notify.data.insert(NDR_POPUP_HTML,getTagFormat(AContactJid));

            FNotifies.insert(FNotifications->appendNotification(notify),AContactJid);
        }
    }
}

void UserTuneHandler::onNotificationActivated(int ANotifyId)
{
    if (FNotifies.contains(ANotifyId))
    {
        FNotifications->removeNotification(ANotifyId);
    }
}

void UserTuneHandler::onNotificationRemoved(int ANotifyId)
{
    if (FNotifies.contains(ANotifyId))
    {
        FNotifies.remove(ANotifyId);
    }
}
#ifdef Q_WS_X11
void UserTuneHandler::updateFetchers()
{
    if (FMetaDataFetcher)
    {
        delete FMetaDataFetcher;
        FMetaDataFetcher = NULL;
    }

    switch (Options::node(OPV_UT_PLAYER_VER).value().toUInt()) {
#ifdef Q_WS_X11
    case mprisV1:
        FMetaDataFetcher = new MprisFetcher1(this, Options::node(OPV_UT_PLAYER_NAME).value().toString());
        break;
    case mprisV2:
        FMetaDataFetcher = new MprisFetcher2(this, Options::node(OPV_UT_PLAYER_NAME).value().toString());
        break;
#elif Q_WS_WIN
    // for Windows players...
#endif
    case fetcherNone:
        // disable send data, only recive
        break;
    default:
        break;
    }

    if (FMetaDataFetcher)
    {
        connect(FMetaDataFetcher, SIGNAL(trackChanged(UserTuneData)), this, SLOT(onTrackChanged(UserTuneData)));
        connect(FMetaDataFetcher, SIGNAL(statusChanged(PlayerStatus)), this, SLOT(onPlayerSatusChanged(PlayerStatus)));
    }
    else
    {
        onStopPublishing();
    }
}
#endif
bool UserTuneHandler::processPEPEvent(const Jid &AStreamJid, const Stanza &AStanza)
{
    Q_UNUSED(AStreamJid)
    Jid senderJid;
    UserTuneData userSong;

    QDomElement replyElem = AStanza.document().firstChildElement("message");

    if (!replyElem.isNull())
    {
        senderJid = replyElem.attribute("from");
        QDomElement eventElem = replyElem.firstChildElement("event");

        if (!eventElem.isNull())
        {
            QDomElement itemsElem = eventElem.firstChildElement("items");

            if (!itemsElem.isNull())
            {
                QDomElement itemElem = itemsElem.firstChildElement("item");

                if (!itemElem.isNull())
                {
                    QDomElement tuneElem = itemElem.firstChildElement("tune");

                    if (!tuneElem.isNull() && !tuneElem.firstChildElement().isNull())
                    {
                        QDomElement elem;
                        elem = tuneElem.firstChildElement("artist");
                        if (!elem.isNull())
                        {
                            userSong.artist = elem.text();
                        }

                        elem = tuneElem.firstChildElement("length");
                        if (!elem.isNull())
                        {
                            userSong.length = elem.text().toUInt();
                        }

                        elem = tuneElem.firstChildElement("rating");
                        if (!elem.isNull())
                        {
                            userSong.rating = elem.text().toUInt();
                        }

                        elem = tuneElem.firstChildElement("source");
                        if (!elem.isNull())
                        {
                            userSong.source = elem.text();
                        }

                        elem = tuneElem.firstChildElement("title");
                        if (!elem.isNull())
                        {
                            userSong.title = elem.text();
                        }

                        elem = tuneElem.firstChildElement("track");
                        if (!elem.isNull())
                        {
                            userSong.track = elem.text();
                        }

                        elem = tuneElem.firstChildElement("uri");
                        if (!elem.isNull())
                        {
                            userSong.uri = elem.text();
                        }

                        setContactLabel(senderJid);
                        onShowNotification(AStreamJid, senderJid);
                    }
                    else // !tuneElem.isNull() && !tuneElem.firstChildElement().isNull()
                    {
                        unsetContactLabel(senderJid);
                    }
                }
            }
        }
    }

    setContactTune(senderJid, userSong);

    return true;
}

#ifdef Q_WS_X11
void UserTuneHandler::onTrackChanged(UserTuneData data)
{
    if (FTimer.isActive())
    {
        FTimer.stop();
    }

    FUserTuneData = data;

    FTimer.start();
}

void UserTuneHandler::onSendPep()
{
    QDomDocument doc("");
    QDomElement root = doc.createElement("item");
    doc.appendChild(root);

    QDomElement tune = doc.createElement("tune");
    root.appendChild(tune);

    ADD_CHILD_ELEMENT (doc, tune, "artist", FUserTuneData.artist)

    if (FUserTuneData.length > 0) {
        ADD_CHILD_ELEMENT (doc, tune, "length", QString::number(FUserTuneData.length))
    }

    ADD_CHILD_ELEMENT (doc, tune, "rating", QString::number(FUserTuneData.rating))
    ADD_CHILD_ELEMENT (doc, tune, "source", FUserTuneData.source)
    ADD_CHILD_ELEMENT (doc, tune, "title", FUserTuneData.title)
    ADD_CHILD_ELEMENT (doc, tune, "track", FUserTuneData.track)
    ADD_CHILD_ELEMENT (doc, tune, "uri", FUserTuneData.uri.toString())

#ifndef QT_NO_DEBUG
    qDebug() << doc.toString();
#endif
    Jid streamJid;
    int streams_size = FXmppStreams->xmppStreams().size();

    for (int i = 0; i < streams_size; i++)
    {
        streamJid = FXmppStreams->xmppStreams().at(i)->streamJid();
        FPEPManager->publishItem(streamJid, TUNE_PROTOCOL_URL, root);
    }
}

void UserTuneHandler::onPlayerSatusChanged(PlayerStatus status)
{
    if (status.Play == PSStopped) {
        onStopPublishing();
    }
}

void UserTuneHandler::onStopPublishing()
{
    QDomDocument doc("");
    QDomElement root = doc.createElement("item");
    doc.appendChild(root);

    QDomElement tune = doc.createElement("tune");
    root.appendChild(tune);

    Jid streamJid;
    IXmppStream *stream = qobject_cast<IXmppStream *>(sender());

    if (stream != NULL)
    {
        streamJid = stream->streamJid();
        FPEPManager->publishItem(streamJid, TUNE_PROTOCOL_URL, root);
        FContactTune.remove(streamJid);
    }
    else
    {
        int streams_size = FXmppStreams->xmppStreams().size();

        for (int i = 0; i < streams_size; i++)
        {
            streamJid = FXmppStreams->xmppStreams().at(i)->streamJid();
            FPEPManager->publishItem(streamJid, TUNE_PROTOCOL_URL, root);
            FContactTune.clear();
        }
    }
}
#endif
/*
    set music icon to main accaunt
*/
void UserTuneHandler::onSetMainLabel(IXmppStream *AXmppStream)
{
    IRosterIndex *index = FRostersModel->streamRoot(AXmppStream->streamJid());
    if (index!=NULL)
    {
        FRostersViewPlugin->rostersView()->insertLabel(FUserTuneLabelId, index);
    }
}

void UserTuneHandler::onUnsetMainLabel(IXmppStream *AXmppStream)
{
    IRosterIndex *index = FRostersModel->streamRoot(AXmppStream->streamJid());
    if (index!=NULL)
    {
        FRostersViewPlugin->rostersView()->removeLabel(FUserTuneLabelId, index);
    }
}

void UserTuneHandler::setContactTune(const Jid &AContactJid, const UserTuneData &ASong)
{
    UserTuneData data = FContactTune.value(AContactJid);
    if (data != ASong)
    {
        if (!ASong.isEmpty())
            FContactTune.insert(AContactJid,ASong);
        else
            FContactTune.remove(AContactJid);
    }
}

/*
    set music icon to contact
*/
void UserTuneHandler::setContactLabel()
{
    if (Options::node(OPV_UT_SHOW_ROSTER_LABEL).value().toBool())
    {
        foreach (const Jid &contactJid, FContactTune.keys())
        {
            QMultiMap<int, QVariant> findData;
            findData.insert(RDR_TYPE,RIT_CONTACT);
            findData.insert(RDR_PREP_BARE_JID,contactJid.pBare());

            foreach (IRosterIndex *index, FRostersModel->rootIndex()->findChilds(findData,true))
            {
                if (contactJid.pBare() == index->data(RDR_PREP_BARE_JID).toString())
                {
                    FRostersViewPlugin->rostersView()->insertLabel(FUserTuneLabelId,index);
                }
                else
                {
                    FRostersViewPlugin->rostersView()->removeLabel(FUserTuneLabelId,index);
                }
            }
        }
    }
}

void UserTuneHandler::setContactLabel(const Jid &AContactJid)
{
    if (Options::node(OPV_UT_SHOW_ROSTER_LABEL).value().toBool())
    {
        QMultiMap<int, QVariant> findData;
        findData.insert(RDR_TYPE,RIT_CONTACT);
        findData.insert(RDR_PREP_BARE_JID,AContactJid.pBare());

        foreach (IRosterIndex *index, FRostersModel->rootIndex()->findChilds(findData,true))
        {
            if (AContactJid.pBare() == index->data(RDR_PREP_BARE_JID).toString())
            {
                FRostersViewPlugin->rostersView()->insertLabel(FUserTuneLabelId,index);
            }
            else
            {
                FRostersViewPlugin->rostersView()->removeLabel(FUserTuneLabelId,index);
            }
        }
    }
}

void UserTuneHandler::unsetContactLabel()
{
    if (Options::node(OPV_UT_SHOW_ROSTER_LABEL).value().toBool())
    {
        foreach (const Jid &AContactJid, FContactTune.keys())
        {
            QMultiMap<int, QVariant> findData;
            findData.insert(RDR_TYPE,RIT_CONTACT);
            findData.insert(RDR_PREP_BARE_JID,AContactJid.pBare());

            foreach (IRosterIndex *index, FRostersModel->rootIndex()->findChilds(findData,true))
            {
                FRostersViewPlugin->rostersView()->removeLabel(FUserTuneLabelId,index);
            }
        }
    }
}

void UserTuneHandler::unsetContactLabel(const Jid &AContactJid)
{
    if (Options::node(OPV_UT_SHOW_ROSTER_LABEL).value().toBool())
    {
        QMultiMap<int, QVariant> findData;
        findData.insert(RDR_TYPE,RIT_CONTACT);
        findData.insert(RDR_PREP_BARE_JID,AContactJid.pBare());

        foreach (IRosterIndex *index, FRostersModel->rootIndex()->findChilds(findData,true))
        {
            FRostersViewPlugin->rostersView()->removeLabel(FUserTuneLabelId,index);
        }
    }
}

QString UserTuneHandler::getTagFormat(const Jid &AContactJid)
{
    QString Tag = Qt::escape(FFormatTag);

    Tag.replace(QString("%A"), Qt::escape(FContactTune.value(AContactJid).artist));
    Tag.replace(QString("%L"), Qt::escape(secToTime(FContactTune.value(AContactJid).length)));
    Tag.replace(QString("%R"), Qt::escape(QString::number(FContactTune.value(AContactJid).rating))); // ★☆✮
    Tag.replace(QString("%S"), Qt::escape(FContactTune.value(AContactJid).source));
    Tag.replace(QString("%T"), Qt::escape(FContactTune.value(AContactJid).title));
    Tag.replace(QString("%N"), Qt::escape(FContactTune.value(AContactJid).track));
    Tag.replace(QString("%U"), Qt::escape(FContactTune.value(AContactJid).uri.toString()));

    return Tag;
}

void UserTuneHandler::onRosterIndexToolTips(IRosterIndex *AIndex, int ALabelId, QMultiMap<int,QString> &AToolTips)
{
    if (ALabelId==RLID_DISPLAY || ALabelId==FUserTuneLabelId)
    {
        Jid contactJid = AIndex->data(RDR_PREP_BARE_JID).toString();
        if (FContactTune.contains(contactJid))
        {
            QString tip = QString("%1 <div style='margin-left:10px;'>%2</div>").arg(tr("Listen:")).arg(getTagFormat(contactJid).replace("\n","<br />"));
            AToolTips.insert(RTTO_USERTUNE,tip);
        }
    }
}

void UserTuneHandler::onApplicationQuit()
{
    FPEPManager->removeNodeHandler(handlerId);
}

Q_EXPORT_PLUGIN2(plg_pepmanager, UserTuneHandler)
