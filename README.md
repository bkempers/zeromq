# ZeroMQ CPP Example Project

Learning project for ZeroMQ (C++), meant to implement and focus on message types (PUB/SUB, DEALER/ROUTER) with CurveMQ (ZAP) protocol for authentication. In addition to protobuf based payload for serialized header/command messages.

## CurveMQ Authentication

_ideally wouldnt commit .secret keys_

### Keygeneration

`pip install pyzmq`

```
python3 -c "import zmq; p,s = zmq.curve_keypair(); print(p.decode()); print(s.decode())
```
