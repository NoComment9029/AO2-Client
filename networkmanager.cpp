#include "networkmanager.h"

#include "datatypes.h"
#include "debug_functions.h"
#include "lobby.h"


NetworkManager::NetworkManager(AOApplication *parent) : QObject(parent)
{
  ao_app = parent;

  ms_socket = new QTcpSocket(this);
  server_socket = new QTcpSocket(this);

  QObject::connect(ms_socket, SIGNAL(readyRead()), this, SLOT(handle_ms_packet()));
  QObject::connect(server_socket, SIGNAL(readyRead()), this, SLOT(handle_server_packet()));
  QObject::connect(server_socket, SIGNAL(disconnected()), ao_app, SLOT(server_disconnected()));
}

NetworkManager::~NetworkManager()
{

}

void NetworkManager::connect_to_master()
{
  ms_socket->close();
  ms_socket->abort();

  perform_srv_lookup();
}

void NetworkManager::connect_to_server(server_type p_server)
{
  server_socket->close();
  server_socket->abort();

  server_socket->connectToHost(p_server.ip, p_server.port);
}

void NetworkManager::ship_ms_packet(QString p_packet)
{
  ms_socket->write(p_packet.toUtf8());
}

void NetworkManager::ship_server_packet(QString p_packet)
{
  server_socket->write(p_packet.toUtf8());
}

void NetworkManager::handle_ms_packet()
{
  char buffer[16384] = {0};
  ms_socket->read(buffer, ms_socket->bytesAvailable());

  QString in_data = buffer;

  if (!in_data.endsWith("%"))
  {
    ms_partial_packet = true;
    ms_temp_packet += in_data;
    return;
  }

  else
  {
    if (ms_partial_packet)
    {
      in_data = ms_temp_packet + in_data;
      ms_temp_packet = "";
      ms_partial_packet = false;
    }
  }

  QStringList packet_list = in_data.split("%", QString::SplitBehavior(QString::SkipEmptyParts));

  for (QString packet : packet_list)
  {    
    AOPacket *f_packet = new AOPacket(packet);

    ao_app->ms_packet_received(f_packet);
  }
}

void NetworkManager::perform_srv_lookup()
{
  ms_dns = new QDnsLookup(QDnsLookup::SRV, ms_hostname, this);

  connect(ms_dns, SIGNAL(finished()), this, SLOT(on_srv_lookup()));
  ms_dns->lookup();
}

void NetworkManager::on_srv_lookup()
{
  bool connected = false;
  if (ms_dns->error() != QDnsLookup::NoError)
  {
    qWarning("SRV lookup of the master server DNS failed.");
    ms_dns->deleteLater();
  }
  else
  {
    const auto srv_records = ms_dns->serviceRecords();

    for (const QDnsServiceRecord &record : srv_records)
    {
      qDebug() << "Connecting to " << record.target() << ":" << record.port();
      ms_socket->connectToHost(record.target(), record.port());
      QTime timer;
      timer.start();
      do
      {
        ao_app->processEvents();
        if (ms_socket->state() == QAbstractSocket::ConnectedState)
        {
          connected = true;
          break;
        }
        else if (ms_socket->state() != QAbstractSocket::ConnectingState
                 && ms_socket->state() != QAbstractSocket::HostLookupState
                 && ms_socket->error() != -1)
        {
          qDebug() << ms_socket->error();
          qWarning() << "Error connecting to master server:" << ms_socket->errorString();
          ms_socket->abort();
          ms_socket->close();
          break;
        }
      } while (timer.elapsed() < timeout_milliseconds); // Very expensive spin-wait loop - it will bring CPU to 100%!
      if (connected) break;
      else
      {
        ms_socket->abort();
        ms_socket->close();
      }
    }
  }
  emit ms_connect_finished(connected);
}

void NetworkManager::handle_server_packet()
{
  char buffer[16384] = {0};
  server_socket->read(buffer, server_socket->bytesAvailable());

  QString in_data = buffer;

  if (!in_data.endsWith("%"))
  {
    partial_packet = true;
    temp_packet += in_data;
    return;
  }

  else
  {
    if (partial_packet)
    {
      in_data = temp_packet + in_data;
      temp_packet = "";
      partial_packet = false;
    }
  }

  QStringList packet_list = in_data.split("%", QString::SplitBehavior(QString::SkipEmptyParts));

  for (QString packet : packet_list)
  {
    AOPacket *f_packet = new AOPacket(packet);

    ao_app->server_packet_received(f_packet);
  }
}

