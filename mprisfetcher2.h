#ifndef MPRISFETCHER2_H
#define MPRISFETCHER2_H

#include "imprisfetcher.h"

class MprisFetcher2 : public IMprisFetcher
{
public:
    MprisFetcher2(QObject *parent, const QString &APlayerName);
    ~MprisFetcher2();
    virtual QStringList getPlayersList();
    QVariantMap getMetadata();
    bool isNowPlaying();

signals:
    void statusChanged(PlayingStatus);
    void trackChanged(QVariantMap);

public slots:
    virtual void playerPlay();
    virtual void playerStop();
    virtual void playerPrev();
    virtual void playerNext();
    virtual void onPlayerNameChange(const QString &);

private slots:
    virtual void onPropertyChange(QDBusMessage);
    virtual void onPlayersExistenceChanged(QString, QString, QString);

private:
    void connectToBus();
    void disconnectToBus();
};

#endif // MPRISFETCHER2_H