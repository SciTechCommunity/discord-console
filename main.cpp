#include <secrets>
using namespace qt;
std::random_device rd;
std::mt19937 mrand(rd());

auto _sendHelp() {
  return "```\n"
         "Commands:\n"
         "./start Creates a new Console Instance\n"
         "./reset <id> Resets a the instance with the corresponding id\n"
         "./kill <id> Kills the instance with the corresponding id\n"
         "./login <id> Moves you to the instance with the corresponding id\n"
         "./<stuff> Executes stuff in the current instance\n"
         "./print <stuff> Prints the debug info of stuff\n"
         "./instances Lists currently running instances\n"
         "./help Shows this menu\n"
         "```";
}

net::reply * _await(net::reply * const waitReply)
{
  // Awaits a response
  while (waitReply->isRunning())
    qt::core::processEvents();
  // Schedules reply for deletion
  waitReply->deleteLater();
  return waitReply;
}
net::reply * _getReply(const string & response,
                       const string & CHANNEL,
                       net::manager * const access)
{
  // Creates a new request
  net::request req(string("%1/channels/%2/webhooks").arg(API, CHANNEL));
  req.setRawHeader("Authorization", string("Bot %1").arg(TOKEN).toUtf8());
  net::reply * rep = access->get(req);
  _await(rep);
  json::obj hook =
      json::doc::fromJson(rep->readAll())
      .array()
      .first()
      .toObject();
  req = net::request
        (string("%1/webhooks/%2/%3")
         .arg(API)
         .arg(hook["id"].toString())
        .arg(hook["token"].toString()));

  // Creates request message
  json::obj message {
    {"content", response},
    {"username", USERNAME},
    {"avatar_url", AVATAR}
  };
  // Sets the content header
  req.setRawHeader
      ("Content-Type", "application/json");
  // Sends the request
  return access->post(req, json::doc(message).toJson());
}
json::obj const _parseObj(net::reply * const parseReply)
{
  // Returns reply data or error message as obj
  return parseReply->isReadable() ?
        json::doc::fromJson(parseReply->readAll()).object()
      :
        json::obj {{"error", parseReply->errorString()}};
}
script::var _evalCommand(script::engine * const engine, const string & input)
{
  timer t;
  t.setInterval(10000);
  t.setSingleShot(true);
  core::connect(&t, &timer::timeout, [engine](){
    engine->abortEvaluation("timeout");
  });
  script::var var;
  t.start();
  var = engine->evaluate(input);
  t.stop();
  if (var.isError()) {
    stringlist ret_val;
    ret_val << string("%1 on line %2")
               .arg(var.property("name").toString())
               .arg(var.property("lineNumber").toString());
    ret_val << var.property("message").toString();
    return ret_val.join('\n');
  }
  //  else if (var.isNull()) return "null";
  //  else if (var.isUndefined()) return "undefined";
  else return var;
}
auto _sendResponse(const string & CHANNEL,
                   net::manager * const sender,
                   const string & response)
{
  if (response.isNull()) return json::obj();
  return _parseObj
      (_await
       (_getReply
        (response,
         (CHANNEL),
         sender)));
}

auto _sendReady(net::websocket * const receiver)
{
  receiver->sendTextMessage
      (string
       (json::doc
        (json::obj {
           {"op", 2},
           {"d", json::obj {
              {"token", TOKEN},
              {"properties", json::obj {
                 {"$browser", "shadow was here"},
               }
              },
              {"large_threshold", 50}
            }
           }
         })
        .toJson()));
}
auto _startHeartbeat(net::websocket * const receiver,
                     json::var sequence,
                     int interval)
{
  timer * heartbeat = new timer;
  core::connect(heartbeat, &timer::timeout, [receiver, sequence, heartbeat](){
    if (receiver->state() == QAbstractSocket::ConnectedState) {
      receiver->sendTextMessage
          (json::doc(json::obj {{"op",1},{"d",sequence}}).toJson());
    } else heartbeat->deleteLater();
  });
  heartbeat->start(interval);
}
auto _startConnection(net::websocket * const receiver)
{
  string url = string("wss://gateway.discord.gg/?encoding=json&v=6");
  core::connect(receiver, &net::websocket::connected, [](){
    qDebug() << "OPEN!";
  }); core::connect(receiver, &net::websocket::aboutToClose, [&url, receiver](){
    qDebug() << "CLOSED!";
  }); core::connect(receiver, static_cast<void(QWebSocket::*)
                    (net::socket::SocketError)>(&QWebSocket::error),
                    [](net::socket::SocketError error){
    qDebug() << error;
  }); core::connect(receiver, &net::websocket::stateChanged, [url, receiver](net::socket::SocketState state){
    qDebug() << state;
    if (state == net::socket::UnconnectedState)
      receiver->open(url);
  }); receiver->open(url);
}

map<string,quint8> types
{
  {"GUILD_CREATE", 0},
  {"MESSAGE_CREATE", 1},
};

int main(int argc, char *argv[])
{
  core loop(argc, argv);

  net::manager * const sender = new net::manager(&loop);
  net::websocket * const receiver = new net::websocket();
  hash<quint64, QPointer<script::engine> > instances;
  hash<quint64,quint64> users;
  receiver->setParent(&loop);

  string input = "";//stringstream(stdin).readLine();

  core::connect(receiver, &net::websocket::textMessageReceived,
                [&, receiver](const string & _message)
  {
    json::obj message =
        json::doc::fromJson(_message.toUtf8()).object();
    qDebug() << message;

    switch (message["op"].toInt()) {
    case 0:{
        switch (types.value(message["t"].toString(), -1)) {
        case -1:return;
        case 0:{
          } return;
        case 1:{
            message = message["d"].toObject();
            qDebug().noquote()
                << json::doc(message).toJson(json::doc::Indented);
            qDebug() << "parsing...";
            qDebug() << _sendResponse
                        (message["channel_id"].toString(),
                sender,
                [&, message]() -> string {
              input = message["content"].toString().simplified();
              if (input.startsWith("./start")) {
                if (instances.keys().length() == 8)
                  return "Maximum instances reached!";
                QPointer<script::engine> engine =
                    new script::engine(&loop);
                engine->setProcessEventsInterval(500);
                engine->pushContext();
                script::agent * agent =
                    new script::agent(engine);
                quint64 id = mrand();
                instances.insert(id, engine);
                return string("Starting new console instance: %1")
                    .arg(string().setNum(id, 16).toUpper());
              } else if (input.startsWith("./kill")) {
                quint64 id = input.remove(0,6).toULongLong(nullptr, 16);
                auto instance = instances.value(id);
                if (0 == instances.remove(id)) return "Invalid Instance!";
                instance->abortEvaluation(script::var("Instance Killed!"));
                instance->deleteLater();
                return "Instance terminated...";
              } else if(input.startsWith("./reset")) {
                quint64 id = input.remove(0,7).toULongLong(nullptr, 16);
                auto instance = instances.value(id);
                if (instance) {
                  instance->abortEvaluation(script::var("Instance Restarted!"));
                  instance->popContext();
                  instance->pushContext();
                  return "Resetting instance...";
                } return "Invalid Instance!";
              } else if(input.startsWith("./help")) {
                return _sendHelp();
              } else if(input.startsWith("./login")) {
                quint64 id = input.remove(0,7).toULongLong(nullptr, 16);
                quint64 author = message["author"]
                                 .toObject()["id"]
                                 .toString()
                                 .toULongLong();
                users.insert(author,id);
                return string("Moved User: <@%1> to Instance: %2")
                    .arg(author)
                    .arg(string().setNum(id,16).toUpper());
              } else if (input.startsWith("./print")) {
                quint64 author = message["author"]
                                 .toObject()["id"]
                                 .toString()
                                 .toULongLong();
                auto engine = instances.value(users.value(author));
                if (engine) {
                  auto ret =
                      json::var::fromVariant
                      (_evalCommand(engine, input.remove(0,7))
                       .toVariant());
                  if (ret.isObject()) {
                    return json::doc(ret.toObject())
                        .toJson(json::doc::Indented);
                  } else {
                    return json::doc
                        (json::obj {
                           {"type", string(ret.toVariant().typeName()).remove("Q")},
                           {"value", ret},
                         }).toJson(json::doc::Indented);
                  }
                } else return "You are currently not logged into a running instance!";
              } else if (input.startsWith("./instances")) {
                stringlist instancelist;
                quint64 author = message["author"]
                                 .toObject()["id"]
                                 .toString()
                                 .toULongLong();
                for (auto key : instances.keys()) {
                  instancelist << string("%1%2")
                                  .arg(users.value(author) == key ? ">> ":" ")
                                  .arg(string().setNum(key,16).toUpper());
                } return instancelist.join('\n');
              } else if (input.startsWith("./")) {
                quint64 author = message["author"]
                                 .toObject()["id"]
                                 .toString()
                                 .toULongLong();
                auto engine = instances.value(users.value(author));
                if (engine) {
                  return
                      _evalCommand(engine, input.remove(0,2))
                      .toString();
                }
                else return "You are currently not logged into a running instance!";
              }
              return string();
            }());
          } return;
        default:return;
        }
      } return;
    case 10:{
        _sendReady(receiver);
        _startHeartbeat(receiver, message["s"], message["d"].toObject()["heartbeat_interval"].toInt());
      } return;
    default:return;
    }
  });

  _startConnection(receiver);

  return loop.exec();
}































































































