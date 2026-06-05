import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import 'bridge_protocol_codec.dart';
import 'models.dart';

abstract class MeshAcBridgeClient {
  Stream<List<BridgeScanResult>> get scanResults;
  Stream<BridgeConnectionPhase> get connectionPhase;
  Stream<FeatureNotification> get notifications;

  Future<void> startScan();
  Future<void> stopScan();
  Future<void> connect(String bridgeId);
  Future<void> disconnect();
  Future<List<BridgeDeviceInfo>> readDeviceList();
  Future<List<DeviceStateSnapshot>> readStateSnapshot();
  Future<void> writeCommand(List<int> payload);
  Future<void> subscribeNotifications();
  void dispose();
}

class MeshAcBleService implements MeshAcBridgeClient {
  MeshAcBleService();

  static final Guid _serviceUuid = Guid(BridgeUuids.service);
  static final Guid _deviceListUuid = Guid(BridgeUuids.deviceList);
  static final Guid _stateUuid = Guid(BridgeUuids.state);
  static final Guid _commandUuid = Guid(BridgeUuids.command);
  static final Guid _notifyUuid = Guid(BridgeUuids.notify);

  final _scanResultsController =
      StreamController<List<BridgeScanResult>>.broadcast();
  final _connectionController =
      StreamController<BridgeConnectionPhase>.broadcast();
  final _notificationController =
      StreamController<FeatureNotification>.broadcast();

  final Map<String, BluetoothDevice> _knownDevices = {};
  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<BluetoothConnectionState>? _connectionSubscription;
  StreamSubscription<List<int>>? _notificationSubscription;

  BluetoothDevice? _device;
  BluetoothCharacteristic? _deviceListCharacteristic;
  BluetoothCharacteristic? _stateCharacteristic;
  BluetoothCharacteristic? _commandCharacteristic;
  BluetoothCharacteristic? _notifyCharacteristic;

  @override
  Stream<List<BridgeScanResult>> get scanResults =>
      _scanResultsController.stream;

  @override
  Stream<BridgeConnectionPhase> get connectionPhase =>
      _connectionController.stream;

  @override
  Stream<FeatureNotification> get notifications =>
      _notificationController.stream;

  @override
  Future<void> startScan() async {
    await _requestPermissions();
    _connectionController.add(BridgeConnectionPhase.scanning);
    _scanSubscription ??= FlutterBluePlus.scanResults.listen(
      _handleScanResults,
    );
    await FlutterBluePlus.startScan(
      withServices: [_serviceUuid],
      timeout: const Duration(seconds: 8),
      androidUsesFineLocation: false,
    );
  }

  @override
  Future<void> stopScan() async {
    await FlutterBluePlus.stopScan();
    if (_device == null) {
      _connectionController.add(BridgeConnectionPhase.idle);
    }
  }

  @override
  Future<void> connect(String bridgeId) async {
    await stopScan();
    await disconnect();

    final device = _knownDevices[bridgeId] ?? BluetoothDevice.fromId(bridgeId);
    _device = device;
    _connectionController.add(BridgeConnectionPhase.connecting);
    _connectionSubscription = device.connectionState.listen((state) {
      if (state == BluetoothConnectionState.connected) {
        _connectionController.add(BridgeConnectionPhase.connected);
      } else if (state == BluetoothConnectionState.disconnected) {
        _connectionController.add(BridgeConnectionPhase.disconnected);
      }
    });

    try {
      if (!device.isConnected) {
        await device.connect(
          license: License.nonprofit,
          timeout: const Duration(seconds: 15),
        );
      }
      final services = await device.discoverServices();
      _bindBridgeCharacteristics(services);
      await subscribeNotifications();
      _connectionController.add(BridgeConnectionPhase.connected);
    } catch (_) {
      _connectionController.add(BridgeConnectionPhase.error);
      rethrow;
    }
  }

  @override
  Future<void> disconnect() async {
    await _notificationSubscription?.cancel();
    _notificationSubscription = null;
    await _connectionSubscription?.cancel();
    _connectionSubscription = null;
    final current = _device;
    _device = null;
    _clearCharacteristics();
    if (current != null && current.isConnected) {
      await current.disconnect();
    }
    _connectionController.add(BridgeConnectionPhase.disconnected);
  }

  @override
  Future<List<BridgeDeviceInfo>> readDeviceList() async {
    final payload = await _requireDeviceListCharacteristic().read();
    return BridgeProtocolCodec.decodeDeviceList(payload);
  }

  @override
  Future<List<DeviceStateSnapshot>> readStateSnapshot() async {
    final payload = await _requireStateCharacteristic().read();
    return BridgeProtocolCodec.decodeStateSnapshot(payload);
  }

  @override
  Future<void> writeCommand(List<int> payload) async {
    await _requireCommandCharacteristic().write(
      payload,
      withoutResponse: false,
    );
  }

  @override
  Future<void> subscribeNotifications() async {
    final characteristic = _requireNotifyCharacteristic();
    await _notificationSubscription?.cancel();
    _notificationSubscription = characteristic.onValueReceived.listen((
      payload,
    ) {
      if (payload.isEmpty) {
        return;
      }
      try {
        _notificationController.add(
          BridgeProtocolCodec.decodeNotification(payload),
        );
      } on FormatException {
        // Ignore malformed bridge frames while keeping the subscription alive.
      }
    });
    await characteristic.setNotifyValue(true);
  }

  @override
  void dispose() {
    _scanSubscription?.cancel();
    _connectionSubscription?.cancel();
    _notificationSubscription?.cancel();
    _scanResultsController.close();
    _connectionController.close();
    _notificationController.close();
  }

  void _handleScanResults(List<ScanResult> results) {
    final bridges = <BridgeScanResult>[];
    for (final result in results) {
      final name = result.advertisementData.advName.isNotEmpty
          ? result.advertisementData.advName
          : result.device.platformName;
      final serviceMatched = result.advertisementData.serviceUuids.contains(
        _serviceUuid,
      );
      final nameMatched = name.toLowerCase().contains('meshac bridge');
      if (!serviceMatched && !nameMatched) {
        continue;
      }

      final id = result.device.remoteId.str;
      _knownDevices[id] = result.device;
      bridges.add(
        BridgeScanResult(
          id: id,
          name: name.isEmpty ? 'MeshAC Bridge' : name,
          rssi: result.rssi,
          serviceMatched: serviceMatched,
        ),
      );
    }
    _scanResultsController.add(bridges);
  }

  void _bindBridgeCharacteristics(List<BluetoothService> services) {
    BluetoothService? bridgeService;
    for (final service in services) {
      if (service.uuid == _serviceUuid) {
        bridgeService = service;
        break;
      }
    }
    if (bridgeService == null) {
      throw StateError('MeshAC bridge service A100 was not found');
    }

    for (final characteristic in bridgeService.characteristics) {
      if (characteristic.uuid == _deviceListUuid) {
        _deviceListCharacteristic = characteristic;
      } else if (characteristic.uuid == _stateUuid) {
        _stateCharacteristic = characteristic;
      } else if (characteristic.uuid == _commandUuid) {
        _commandCharacteristic = characteristic;
      } else if (characteristic.uuid == _notifyUuid) {
        _notifyCharacteristic = characteristic;
      }
    }

    if (_deviceListCharacteristic == null ||
        _stateCharacteristic == null ||
        _commandCharacteristic == null ||
        _notifyCharacteristic == null) {
      throw StateError('MeshAC bridge characteristics are incomplete');
    }
  }

  Future<void> _requestPermissions() async {
    if (kIsWeb) {
      return;
    }
    if (Platform.isAndroid) {
      final statuses = await [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
      ].request();
      final denied = statuses.values.any((status) => !status.isGranted);
      if (denied) {
        throw StateError('蓝牙扫描/连接权限未授予');
      }
      await Permission.locationWhenInUse.request();
    } else if (Platform.isIOS || Platform.isMacOS) {
      await Permission.bluetooth.request();
    }
  }

  BluetoothCharacteristic _requireDeviceListCharacteristic() {
    final characteristic = _deviceListCharacteristic;
    if (characteristic == null) {
      throw StateError('Device list characteristic A101 is not ready');
    }
    return characteristic;
  }

  BluetoothCharacteristic _requireStateCharacteristic() {
    final characteristic = _stateCharacteristic;
    if (characteristic == null) {
      throw StateError('State characteristic A102 is not ready');
    }
    return characteristic;
  }

  BluetoothCharacteristic _requireCommandCharacteristic() {
    final characteristic = _commandCharacteristic;
    if (characteristic == null) {
      throw StateError('Command characteristic A103 is not ready');
    }
    return characteristic;
  }

  BluetoothCharacteristic _requireNotifyCharacteristic() {
    final characteristic = _notifyCharacteristic;
    if (characteristic == null) {
      throw StateError('Notify characteristic A104 is not ready');
    }
    return characteristic;
  }

  void _clearCharacteristics() {
    _deviceListCharacteristic = null;
    _stateCharacteristic = null;
    _commandCharacteristic = null;
    _notifyCharacteristic = null;
  }
}
