import 'dart:async';
import 'dart:collection';

import 'package:flutter/foundation.dart';

import 'bridge_protocol_codec.dart';
import 'mesh_ac_ble_service.dart';
import 'models.dart';

class DeviceRepository extends ChangeNotifier {
  DeviceRepository(this._client) {
    _scanSubscription = _client.scanResults.listen((results) {
      _scanResults = List.unmodifiable(results);
      notifyListeners();
    });
    _connectionSubscription = _client.connectionPhase.listen((phase) {
      _phase = phase;
      notifyListeners();
    });
    _notificationSubscription = _client.notifications.listen(
      _applyNotification,
    );
  }

  final MeshAcBridgeClient _client;

  StreamSubscription<List<BridgeScanResult>>? _scanSubscription;
  StreamSubscription<BridgeConnectionPhase>? _connectionSubscription;
  StreamSubscription<FeatureNotification>? _notificationSubscription;

  BridgeConnectionPhase _phase = BridgeConnectionPhase.idle;
  List<BridgeScanResult> _scanResults = const [];
  final Map<int, MonitorDevice> _devices = {};
  String? _connectedBridgeId;
  String? _connectedBridgeName;
  String? _message;
  bool _busy = false;

  BridgeConnectionPhase get phase => _phase;
  bool get isBusy => _busy;
  bool get isConnected => _phase == BridgeConnectionPhase.connected;
  String? get connectedBridgeId => _connectedBridgeId;
  String? get connectedBridgeName => _connectedBridgeName;
  String? get message => _message;
  List<BridgeScanResult> get scanResults => _scanResults;

  UnmodifiableListView<MonitorDevice> get devices {
    final values = _devices.values.toList()
      ..sort((a, b) => a.addr.compareTo(b.addr));
    return UnmodifiableListView(values);
  }

  int get onlineCount =>
      _devices.values.where((device) => device.isOnline).length;

  Future<void> startScan() async {
    await _guarded(() async {
      _message = null;
      await _client.startScan();
    });
  }

  Future<void> connect(BridgeScanResult result) async {
    await _guarded(() async {
      _connectedBridgeId = result.id;
      _connectedBridgeName = result.name;
      await _client.connect(result.id);
      await _readBridgeState(requestRefresh: false);
      _message = '已连接 ${result.name}';
    });
  }

  Future<void> disconnect() async {
    await _guarded(() async {
      await _client.disconnect();
      _connectedBridgeId = null;
      _connectedBridgeName = null;
      _devices.clear();
      _message = '已断开连接';
    });
  }

  Future<void> refreshAll() async {
    await _guarded(() async {
      await _readBridgeState(requestRefresh: true);
      _message = '状态已刷新';
    });
  }

  Future<void> setFeature({
    required int addr,
    required int featureId,
    required int value,
    FeatureType type = FeatureType.intValue,
  }) async {
    await _guarded(() async {
      await _client.writeCommand(
        BridgeProtocolCodec.encodeSetFeatureCommand(
          addr: addr,
          featureId: featureId,
          value: value,
        ),
      );
      _applyNotification(
        FeatureNotification(
          addr: addr,
          featureId: featureId,
          type: type,
          value: value,
        ),
        notify: false,
      );
      _message = '${hex16(addr)} 控制已发送';
    });
  }

  Future<void> setGroupFeature({
    required int featureId,
    required int value,
    FeatureType type = FeatureType.intValue,
  }) async {
    await _guarded(() async {
      await _client.writeCommand(
        BridgeProtocolCodec.encodeGroupSetFeatureCommand(
          featureId: featureId,
          value: value,
        ),
      );
      for (final device in devices) {
        if (!device.isOnline) {
          continue;
        }
        _devices[device.addr] = device.applyFeature(
          FeatureNotification(
            addr: device.addr,
            featureId: featureId,
            type: type,
            value: value,
          ),
        );
      }
      _message = '群组控制已发送';
    });
  }

  MonitorDevice? deviceByAddr(int addr) => _devices[addr];

  @override
  void dispose() {
    _scanSubscription?.cancel();
    _connectionSubscription?.cancel();
    _notificationSubscription?.cancel();
    _client.dispose();
    super.dispose();
  }

  Future<void> _readBridgeState({required bool requestRefresh}) async {
    if (requestRefresh) {
      await _client.writeCommand(BridgeProtocolCodec.encodeRefreshAllCommand());
      await Future<void>.delayed(const Duration(milliseconds: 300));
    }

    final infos = await _client.readDeviceList();
    for (final info in infos) {
      _devices[info.addr] =
          (_devices[info.addr] ?? MonitorDevice.fromInfo(info)).applyInfo(info);
    }

    final snapshots = await _client.readStateSnapshot();
    for (final snapshot in snapshots) {
      final existing =
          _devices[snapshot.addr] ??
          MonitorDevice(
            addr: snapshot.addr,
            isOnline: false,
            profileLoaded: false,
            profileId: 0,
          );
      _devices[snapshot.addr] = existing.applySnapshot(snapshot);
    }
  }

  void _applyNotification(
    FeatureNotification notification, {
    bool notify = true,
  }) {
    final existing =
        _devices[notification.addr] ??
        MonitorDevice(
          addr: notification.addr,
          isOnline: true,
          profileLoaded: false,
          profileId: 0,
        );
    _devices[notification.addr] = existing.applyFeature(notification);
    if (notify) {
      notifyListeners();
    }
  }

  Future<void> _guarded(Future<void> Function() action) async {
    _busy = true;
    notifyListeners();
    try {
      await action();
    } catch (error) {
      _message = error.toString();
      if (_phase != BridgeConnectionPhase.connected) {
        _phase = BridgeConnectionPhase.error;
      }
    } finally {
      _busy = false;
      notifyListeners();
    }
  }
}
