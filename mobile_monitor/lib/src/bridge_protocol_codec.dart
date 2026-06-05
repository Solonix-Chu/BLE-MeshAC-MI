import 'dart:typed_data';

import 'models.dart';

abstract final class BridgeUuids {
  static const service = 'A100';
  static const deviceList = 'A101';
  static const state = 'A102';
  static const command = 'A103';
  static const notify = 'A104';
}

abstract final class BridgeCommandIds {
  static const setFeature = 0x01;
  static const groupSetFeature = 0x02;
  static const refreshAll = 0x03;
}

abstract final class BridgeProtocolCodec {
  static List<BridgeDeviceInfo> decodeDeviceList(List<int> payload) {
    if (payload.isEmpty) {
      return const [];
    }

    final data = Uint8List.fromList(payload);
    final view = ByteData.sublistView(data);
    final count = data[0];
    var offset = 1;
    final devices = <BridgeDeviceInfo>[];

    for (var i = 0; i < count && offset + 6 <= data.length; i++) {
      devices.add(
        BridgeDeviceInfo(
          addr: view.getUint16(offset, Endian.little),
          isOnline: data[offset + 2] != 0,
          profileLoaded: data[offset + 3] != 0,
          profileId: view.getUint16(offset + 4, Endian.little),
        ),
      );
      offset += 6;
    }

    return devices;
  }

  static List<DeviceStateSnapshot> decodeStateSnapshot(List<int> payload) {
    if (payload.isEmpty) {
      return const [];
    }

    final data = Uint8List.fromList(payload);
    final view = ByteData.sublistView(data);
    final count = data[0];
    var offset = 1;
    final snapshots = <DeviceStateSnapshot>[];

    for (var i = 0; i < count && offset + 3 <= data.length; i++) {
      final addr = view.getUint16(offset, Endian.little);
      offset += 2;
      final stateCount = data[offset++];
      final states = <FeatureState>[];

      for (var j = 0; j < stateCount && offset + 7 <= data.length; j++) {
        final featureId = view.getUint16(offset, Endian.little);
        final type = FeatureType.fromCode(data[offset + 2]);
        final value = view.getInt32(offset + 3, Endian.little);
        states.add(
          FeatureState(featureId: featureId, type: type, value: value),
        );
        offset += 7;
      }

      snapshots.add(DeviceStateSnapshot(addr: addr, states: states));
    }

    return snapshots;
  }

  static FeatureNotification decodeNotification(List<int> payload) {
    if (payload.length < 9) {
      throw const FormatException(
        'Bridge notification must be at least 9 bytes',
      );
    }

    final data = Uint8List.fromList(payload);
    final view = ByteData.sublistView(data);
    return FeatureNotification(
      addr: view.getUint16(0, Endian.little),
      featureId: view.getUint16(2, Endian.little),
      type: FeatureType.fromCode(data[4]),
      value: view.getInt32(5, Endian.little),
    );
  }

  static Uint8List encodeSetFeatureCommand({
    required int addr,
    required int featureId,
    required int value,
  }) {
    final data = Uint8List(9);
    final view = ByteData.sublistView(data);
    data[0] = BridgeCommandIds.setFeature;
    view.setUint16(1, addr, Endian.little);
    view.setUint16(3, featureId, Endian.little);
    view.setInt32(5, value, Endian.little);
    return data;
  }

  static Uint8List encodeGroupSetFeatureCommand({
    required int featureId,
    required int value,
  }) {
    final data = Uint8List(7);
    final view = ByteData.sublistView(data);
    data[0] = BridgeCommandIds.groupSetFeature;
    view.setUint16(1, featureId, Endian.little);
    view.setInt32(3, value, Endian.little);
    return data;
  }

  static Uint8List encodeRefreshAllCommand() {
    return Uint8List.fromList(const [BridgeCommandIds.refreshAll]);
  }
}
