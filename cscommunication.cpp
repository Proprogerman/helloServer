#include "cscommunication.h"

#include "updatingdata.h"

#include <QDebug>
#include <QVariant>
#include <QList>
#include <QVector>

#include <QImage>

#include <QByteArray>
#include <QBuffer>

#include <QJsonArray>

#include <iterator>

CSCommunication::CSCommunication(QTcpSocket *tcpSocket, QObject *parent) : respSock(tcpSocket),
    QObject(parent)
{
    connectToDatabase();
    respSock->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(this, &CSCommunication::nameAvailable, this, &CSCommunication::onNameAvailable);
    connect(this, &CSCommunication::clientNameChanged, this, &CSCommunication::onClientNameChanged);
}

CSCommunication::~CSCommunication()
{
    setUserOffline();
}

void CSCommunication::connectToDatabase(){
    database = QSqlDatabase::addDatabase("QMYSQL");
    database.setHostName("127.0.0.1");
    database.setDatabaseName("appschema");
    database.setPort(3306);
    database.setUserName("root");
    database.setPassword("root");
    if(!database.open())
        qDebug()<<"database is not open!";
    else
        qDebug()<<"database is open...";
}

void CSCommunication::processingRequest(QJsonObject &jsonObj){
    const QString requestType = jsonObj["requestType"].toString();
    qDebug()<<requestType;
    if(jsonObj.contains("user_name") && clientName.isEmpty()){
        clientName = jsonObj["user_name"].toString();
        emit clientNameChanged();
    }

    if(requestType == "checkName"){
        checkName(jsonObj["user_name"].toString());
    }
    else if(requestType == "signUp"){
        signUp(jsonObj);
    }
    else if(requestType == "getUserData"){
        getUserData(jsonObj);
    }
    else if(requestType == "getMemeDataForUser"){
        getMemeDataForUser(jsonObj["meme_name"].toString(), jsonObj["user_name"].toString());
    }
    else if(requestType == "getMemeData"){
        getMemeData(jsonObj["meme_name"].toString());
    }
    else if(requestType == "getMemeListWithCategory"){
        getMemeListWithCategory(jsonObj);
    }
    else if(requestType == "getMemesCategories"){
        getMemesCategories();
    }
    else if(requestType == "forceMeme"){
        forceMeme(jsonObj);
    }
    else if(requestType == "increaseLikesQuantity"){
        increaseLikesQuantity(jsonObj);
    }
    else if(requestType == "unforceMeme"){
        unforceMeme(jsonObj["meme_name"].toString(), jsonObj["user_name"].toString());
    }
}

void CSCommunication::setUserOffline()
{
    if(!clientName.isEmpty()){
        QSqlQuery query(database);
        QString queryString = QString("UPDATE users SET online = 0 WHERE name = '%1';")
                                     .arg(clientName);
        if(!query.exec(queryString))
            database.open();
    }
}

void CSCommunication::onNameAvailable(bool val){
    QJsonObject jsonObj{
        { "responseType", "checkNameResponse" },
        { "nameAvailable", val }
    };

    respSock->write(QJsonDocument(jsonObj).toBinaryData());
    respSock->flush();
    respSock->waitForBytesWritten(3000);
    QThread::msleep(100);
}

void CSCommunication::onClientNameChanged()
{
    QSqlQuery query(database);
    QString queryString = QString("UPDATE users SET online = 1 WHERE name = '%1';")
                                 .arg(clientName);
    if(!query.exec(queryString))
        database.open();

}

void CSCommunication::checkName(const QString &name){
    QSqlQuery query(database);
    query.prepare("SELECT * FROM users WHERE name = :name;");
    query.bindValue(":name", name);

    if(query.exec()){
        if(query.size() == 0)
        {
            qDebug()<<"name " << name << " available";
            emit nameAvailable(true);
        }
        else{
            qDebug()<<"name " << name << "not available";
            emit nameAvailable(false);
        }
    }
    else
        database.open();
}

void CSCommunication::signUp(QJsonObject &jsonObj){
    QSqlQuery query(database);
    query.prepare("INSERT INTO users (name, password) VALUES(:name, :password);");
    query.bindValue(":name", jsonObj["user_name"].toString());
    query.bindValue(":password", jsonObj["user_password"].toString());
    qDebug()<<"signUp(): \n"<<"name: "<<jsonObj["user_name"].toString()<<"\n"<<jsonObj["user_password"].toString()<<endl;
    if(!query.exec())
        database.open();
}

void CSCommunication::getUserData(const QJsonObject &jsonObj)
{    
    QSqlQuery userQuery(database);
    QString userQueryString = QString( "SELECT pop_value, creativity, shekels FROM users WHERE name = '%1';")
                                       .arg(jsonObj["user_name"].toString());

    QSqlQuery memesQuery(database);
    QString memesQueryString = QString( "SELECT memes.name, memes.pop_values, startPopValue, memes.image FROM users "
                                        "INNER JOIN user_memes ON users.id = user_id "
                                        "INNER JOIN memes ON meme_id = memes.id WHERE users.name = '%1';")
                                        .arg(jsonObj["user_name"].toString());

    QJsonObject responseObj;
    responseObj.insert("responseType", "getUserDataResponse");

    if(userQuery.exec(userQueryString)){
        QSqlRecord userRec = userQuery.record();
        int userPopValueIndex = userRec.indexOf("pop_value");
        int userCreativityIndex = userRec.indexOf("creativity");
        int userShekelsIndex = userRec.indexOf("shekels");
        userQuery.first();
        responseObj.insert("pop_value", userQuery.value(userPopValueIndex).toInt());
        responseObj.insert("creativity", userQuery.value(userCreativityIndex).toInt());
        responseObj.insert("shekels", userQuery.value(userShekelsIndex).toInt());
    }
    else
        database.open();

    if(memesQuery.exec(memesQueryString)){
        if(memesQuery.size() > 0){
            QSqlRecord rec = memesQuery.record();
            qDebug()<<"query record:"<<rec;
            int memeNameIndex = rec.indexOf("name");
            int memePopIndex = rec.indexOf("pop_values");
            int startPopValueIndex = rec.indexOf("startPopValue");
            int memeImageUrlIndex = rec.indexOf("image");

            qDebug() << "QUERY SIZE= " << memesQuery.size();

            QVariantList memeList;
            QVector<QJsonObject> memeImageVector;

            QVariantList memesToUpdatePopValues = jsonObj.value("updateOnlyPopValues").toArray().toVariantList();
            qDebug()<<"OOOOOOOOOOOOOOOOOOOO ::::::::::: " << memesToUpdatePopValues;

            while(memesQuery.next()){
                QVariantMap memeObj;
                memeObj.insert("memeName", memesQuery.value(memeNameIndex).toString());
                memeObj.insert("popValues", QJsonDocument::fromJson(memesQuery.value(memePopIndex).toByteArray()).array());
                memeObj.insert("startPopValue", memesQuery.value(startPopValueIndex).toInt());
                //qDebug()<<"POP VALUES FROM GETMEMELISTWITHCATEGORY ::::::::" << QJsonDocument::fromJson(memesQuery.value(memePopIndex).toByteArray()).array();
                if(!memesToUpdatePopValues.contains(memesQuery.value(memeNameIndex).toString())){
                        qDebug()<<"NE SODERJIT!!!!!!!!!!!!!!!!!!!!!!!!!!";
                        QImage memeImage/*FullSize*/;
                        QJsonObject memeImageObj;
                        memeImage/*FullSize*/.load(memesQuery.value(memeImageUrlIndex).toString(), "JPG");
                        //QImage memeImage = memeImageFullSize.scaled(100, 100, Qt::KeepAspectRatio);
                        QByteArray byteArr;
                        QBuffer buff(&byteArr);
                        buff.open(QIODevice::WriteOnly);
                        memeImage.save(&buff, "JPG");
                        auto encoded = buff.data().toBase64();
                        memeObj.insert("imageName",QUrl(memesQuery.value(memeImageUrlIndex).toString()).fileName());
                        memeImageObj.insert("responseType", "memeImageResponse");
                        memeImageObj.insert("memeName", memesQuery.value(memeNameIndex).toString());
                        memeImageObj.insert("imageName",QUrl(memesQuery.value(memeImageUrlIndex).toString()).fileName());
                        memeImageObj.insert("imageData", QJsonValue(QString::fromLatin1(encoded)));
                        memeImageVector.append(memeImageObj);
                }
                memeList << memeObj;
                memeObj.clear();
            }
            memesQuery.first();

            responseObj.insert("memeList", QJsonArray::fromVariantList(memeList));
            respSock->write(QJsonDocument(responseObj).toBinaryData());
            respSock->flush();
            respSock->waitForBytesWritten(3000);
            QThread::msleep(100);
            for(int i = 0; i < memeImageVector.size(); i++){
                respSock->write(QJsonDocument(memeImageVector[i]).toBinaryData());
                respSock->flush();
                respSock->waitForBytesWritten(3000);
                QThread::msleep(100);
            }
        }
        else{
            responseObj.insert("memeList", QJsonArray());
            respSock->write(QJsonDocument(responseObj).toBinaryData());
            respSock->flush();
            respSock->waitForBytesWritten(3000);
        }
    }
    else
        database.open();
}

void CSCommunication::getMemeListWithCategory(const QJsonObject &jsonObj)
{
    QSqlQuery query(database);
    QString category = jsonObj.value("category").toString();
    QString queryString = QString("SELECT name, image, pop_values FROM memes WHERE category = '%1';")
            .arg(category);
    if(query.exec(queryString)){
        QSqlRecord rec = query.record();
        qDebug()<<"query record:"<<rec;
        qDebug()<<"category: " << category;
        int memeNameIndex = rec.indexOf("name");
        int memePopIndex = rec.indexOf("pop_values");
        int memeImageUrlIndex = rec.indexOf("image");

        QVariantList memeList;
        QVector<QJsonObject> memeImageVector;

        QVariantList memesToUpdatePopValues = jsonObj.value("updateOnlyPopValues").toArray().toVariantList();
        qDebug()<<"OOOOOOOOOOOOOOOOOOOO ::::::::::: " << memesToUpdatePopValues;

        while(query.next()){
            QVariantMap memeObj;
            memeObj.insert("memeName", query.value(memeNameIndex).toString());
            memeObj.insert("popValues", QJsonDocument::fromJson(query.value(memePopIndex).toByteArray()).array());
            qDebug()<<"POP VALUES FROM GETMEMELISTOFUSER ::::::::" << QJsonDocument::fromJson(query.value(memePopIndex).toByteArray()).array();
            if(!memesToUpdatePopValues.contains(query.value(memeNameIndex).toString())){
                    qDebug()<<"NE SODERJIT!!!!!!!!!!!!!!!!!!!!!!!!!!";
                    QImage memeImage;
                    QJsonObject memeImageObj;
                    memeImage.load(query.value(memeImageUrlIndex).toString(), "JPG");
                    QByteArray byteArr;
                    QBuffer buff(&byteArr);
                    buff.open(QIODevice::WriteOnly);
                    memeImage.save(&buff, "JPG");
                    auto encoded = buff.data().toBase64();
                    memeObj.insert("imageName",QUrl(query.value(memeImageUrlIndex).toString()).fileName());
                    memeImageObj.insert("responseType", "memeImageResponse");
                    memeImageObj.insert("memeName", query.value(memeNameIndex).toString());
                    memeImageObj.insert("imageName",QUrl(query.value(memeImageUrlIndex).toString()).fileName());
                    memeImageObj.insert("imageData", QJsonValue(QString::fromLatin1(encoded)));
                    memeImageVector.append(memeImageObj);
            }
            memeList << memeObj;
            memeObj.clear();
        }
        QJsonObject jsonMemeList;
        jsonMemeList.insert("responseType", "getMemeListWithCategoryResponse");
        jsonMemeList.insert("category", category);
        jsonMemeList.insert("memeList", QJsonArray::fromVariantList(memeList));
        respSock->write(QJsonDocument(jsonMemeList).toBinaryData());
        respSock->flush();
        respSock->waitForBytesWritten(3000);
        QThread::msleep(100);
        for(int i = 0; i < memeImageVector.size(); i++){
            respSock->write(QJsonDocument(memeImageVector[i]).toBinaryData());
            respSock->flush();
            respSock->waitForBytesWritten(3000);
            QThread::msleep(100);
        }
    }
    else
        database.open();
}

void CSCommunication::getMemeDataForUser(const QString &memeName, const QString &userName)
{
    qDebug()<<"MEME_NAME" << memeName;
    QSqlQuery query(database);
    QString queryString = QString("SELECT pop_values, startPopValue FROM memes "
                                  "INNER JOIN user_memes ON memes.id = meme_id "
                                  "INNER JOIN users ON users.id = user_id WHERE memes.name = '%1' AND users.name = '%2';")
                                  .arg(memeName)
                                  .arg(userName);
    if(query.exec(queryString)){
        QSqlRecord rec = query.record();
        qDebug()<<"query record: "<<rec;
        qDebug()<<"query size: "<<query.size();
        int memePopIndex = rec.indexOf("pop_values");
        int startPopValueIndex = rec.indexOf("startPopValue");

        query.first();

        QJsonObject memeDataResponse;
        memeDataResponse.insert("responseType", "getMemeDataForUserResponse");
        memeDataResponse.insert("memeName", memeName);
        memeDataResponse.insert("popValues", QJsonDocument::fromJson(query.value(memePopIndex).toByteArray()).array());
        memeDataResponse.insert("startPopValue", query.value(startPopValueIndex).toInt());
        qDebug()<<"POP VALUES FOR MEME " << memeName << " ::::::: " << QJsonDocument::fromJson(query.value(memePopIndex).toByteArray()).array();
        respSock->write(QJsonDocument(memeDataResponse).toBinaryData());
        respSock->flush();
        respSock->waitForBytesWritten(3000);
        QThread::msleep(100);
    }
    else
        database.open();
}

void CSCommunication::getMemeData(const QString &memeName)
{
    qDebug()<<"MEME_NAME" << memeName;
    QSqlQuery query(database);
    QString queryString = QString("SELECT pop_values FROM memes WHERE memes.name = '%1';")
                                  .arg(memeName);
    if(query.exec(queryString)){
        QSqlRecord rec = query.record();
        qDebug()<<"query record:"<<rec;
        int memePopIndex = rec.indexOf("pop_values");

        query.first();

        QJsonObject memeDataResponse;
        memeDataResponse.insert("responseType", "getMemeDataResponse");
        memeDataResponse.insert("memeName", memeName);
        memeDataResponse.insert("popValues", QJsonDocument::fromJson(query.value(memePopIndex).toByteArray()).array());
        qDebug()<<"POP VALUES FOR MEME " << memeName << " ::::::: " << QJsonDocument::fromJson(query.value(memePopIndex).toByteArray()).array();
        respSock->write(QJsonDocument(memeDataResponse).toBinaryData());
        respSock->flush();
        respSock->waitForBytesWritten(3000);
        QThread::msleep(100);
    }
    else
        database.open();
}

void CSCommunication::getMemesCategories()
{
    QSqlQuery query(database);
    if(query.exec("SELECT DISTINCT category FROM memes;")){
        QSqlRecord rec = query.record();
        qDebug()<<"query record:"<<rec;
        int categoryIndex = rec.indexOf("category");
        QJsonArray categoryArr;

        while(query.next()){
            categoryArr.append(QJsonValue(query.value(categoryIndex).toString()));
        }
        QJsonObject categoriesResponse;
        categoriesResponse.insert("responseType", "getMemesCategoriesResponse");
        categoriesResponse.insert("categories", categoryArr);
        respSock->write(QJsonDocument(categoriesResponse).toBinaryData());
        respSock->flush();
        respSock->waitForBytesWritten(3000);
        QThread::msleep(100);
    }
    else
        database.open();
}

void CSCommunication::forceMeme(const QJsonObject &jsonObj)
{
    qDebug()<<"FORCE MEME";
    QSqlQuery query(database);
    QString memeName = jsonObj.value("meme_name").toString();
    QString userName = jsonObj.value("user_name").toString();
    double creativity = jsonObj.value("creativity").toInt();
    QString queryString = QString(  "SELECT users.id as user_id, memes.id as meme_id, weight FROM users "
                                    "INNER JOIN memes ON memes.name = '%1' "
                                    "INNER JOIN category_weight ON category_weight.category = memes.category "
                                    "WHERE users.name = '%2';")
                                    .arg(memeName)
                                    .arg(userName);
    if(query.exec(queryString)){
        QSqlRecord rec = query.record();
        int userIdIndex = rec.indexOf("user_id");
        int memeIdIndex = rec.indexOf("meme_id");
        int weightIndex = rec.indexOf("weight");
        query.first();
        QSqlQuery forceQuery(database);
        double feedbackRate = query.value(weightIndex).toDouble() * ( 1 + static_cast<double>(creativity) / 100 );
        qDebug()<<"'''''''''''''''''''''''''''''''''";
        forceQuery.exec(QString("INSERT INTO user_memes (user_id, meme_id, startPopValue, creativity, feedbackRate) "
                                "VALUES ('%1', '%2', '%3', '%4', '%5');")
                                .arg(query.value(userIdIndex).toInt())
                                .arg(query.value(memeIdIndex).toInt())
                                .arg(jsonObj.value("startPopValue").toInt())
                                .arg(creativity)
                                .arg(feedbackRate));
        qDebug()<<"INSERT ERROR:::::::"<<forceQuery.lastError();
        qDebug()<<"START POP VALUE: "<<jsonObj.value("startPopValue").toInt();
        if(creativity > 0){
            QSqlQuery updateCreativityQuery(database);
            updateCreativityQuery.exec(QString("UPDATE users SET creativity = creativity - '%1' WHERE name = '%2';")
                                       .arg(creativity)
                                       .arg(userName));
        }
    }
    else
        database.open();
}

void CSCommunication::unforceMeme(const QString &memeName, const QString &userName)
{
    qDebug()<<"UNFORCE MEME";
    QSqlQuery query(database);
    QString queryString = QString(  "DELETE u_m FROM user_memes as u_m "
                                    "INNER JOIN users as u ON user_id = u.id "
                                    "INNER JOIN memes as m ON meme_id = m.id "
                                    "WHERE m.name = '%1' and u.name = '%2';")
                                    .arg(memeName)
                                    .arg(userName);
    query.exec(queryString);
}

void CSCommunication::increaseLikesQuantity(const QJsonObject &jsonObj)
{
    int likePrice = 1;
    int shekels = jsonObj["shekels"].toInt();
    QVariantList popValues = jsonObj["currentPopValues"].toArray().toVariantList();
    int likeIncrement = popValues.last().toInt() + shekels * likePrice;

    if(popValues.size() == memesPopValuesCount){
        for(int i = 0; i < popValues.size() - 1; i++){
            popValues[i] = popValues[i + 1];
        }
        popValues[memesPopValuesCount - 1] = likeIncrement;
    }
    else if(popValues.size() < memesPopValuesCount)
        popValues.append(likeIncrement);

    QSqlQuery likesQuery(database);
    QJsonDocument jsonDoc;
    jsonDoc.setArray(QJsonArray::fromVariantList(popValues));
    QString queryString = QString("UPDATE memes SET pop_values = '%1', edited_by_user = 1 WHERE name='%2';")
            .arg(QString(jsonDoc.toJson(QJsonDocument::Compact)))
            .arg(jsonObj["meme_name"].toString());
    if(likesQuery.exec(queryString)){
        QSqlQuery updateShekelsQuery(database);
        updateShekelsQuery.exec(QString("UPDATE users SET shekels = shekels - '%1' WHERE name = '%2';")
                                       .arg(shekels)
                                       .arg(jsonObj.value("user_name").toString()));
    }
    else
        database.open();
}


