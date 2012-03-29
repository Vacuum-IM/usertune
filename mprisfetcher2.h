#ifndef MPRISFETCHER2_H
#define MPRISFETCHER2_H

#include "imprisfetcher.h"

class MprisFetcher2 : public IMprisFetcher
{
    Q_OBJECT
public:
    MprisFetcher2(QObject *parent, const QString &APlayerName);
    ~MprisFetcher2();
    QVariantMap getMetadata();
    PlayerStatus getPlayerStatus();
    virtual QStringList getPlayersList();

signals:
    void statusChanged(PlayerStatus);
    void trackChanged(UserTuneData);

public slots:
    virtual void playerPlay();
    virtual void playerStop();
    virtual void playerPrev();
    virtual void playerNext();
    virtual void onPlayerNameChange(const QString &);

private slots:
    void onPropertyChange(QDBusMessage);
    virtual void onPlayersExistenceChanged(QString, QString, QString);

private:
    void connectToBus();
    void disconnectToBus();

private:
    UserTuneData FUserTuneData;
};

#endif // MPRISFETCHER2_H
