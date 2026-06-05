import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_monitor/main.dart';
import 'package:mobile_monitor/src/device_repository.dart';
import 'package:mobile_monitor/src/mesh_ac_ble_service.dart';
import 'package:mobile_monitor/src/models.dart';

void main() {
  testWidgets('shows monitor home screen', (tester) async {
    final client = FakeBridgeClient();
    final repository = DeviceRepository(client);

    await tester.pumpWidget(MeshMonitorApp(repository: repository));

    expect(find.text('MeshAC 监控终端'), findsOneWidget);
    expect(find.text('群组控制'), findsOneWidget);
    expect(find.text('暂无设备状态'), findsOneWidget);

    repository.dispose();
  });
}

class FakeBridgeClient implements MeshAcBridgeClient {
  final _scanResults = StreamController<List<BridgeScanResult>>.broadcast();
  final _connectionPhase = StreamController<BridgeConnectionPhase>.broadcast();
  final _notifications = StreamController<FeatureNotification>.broadcast();

  @override
  Stream<List<BridgeScanResult>> get scanResults => _scanResults.stream;

  @override
  Stream<BridgeConnectionPhase> get connectionPhase => _connectionPhase.stream;

  @override
  Stream<FeatureNotification> get notifications => _notifications.stream;

  @override
  Future<void> connect(String bridgeId) async {
    _connectionPhase.add(BridgeConnectionPhase.connected);
  }

  @override
  Future<void> disconnect() async {
    _connectionPhase.add(BridgeConnectionPhase.disconnected);
  }

  @override
  Future<List<BridgeDeviceInfo>> readDeviceList() async => const [];

  @override
  Future<List<DeviceStateSnapshot>> readStateSnapshot() async => const [];

  @override
  Future<void> startScan() async {
    _connectionPhase.add(BridgeConnectionPhase.scanning);
    _scanResults.add(const []);
  }

  @override
  Future<void> stopScan() async {}

  @override
  Future<void> subscribeNotifications() async {}

  @override
  Future<void> writeCommand(List<int> payload) async {}

  @override
  void dispose() {
    _scanResults.close();
    _connectionPhase.close();
    _notifications.close();
  }
}
