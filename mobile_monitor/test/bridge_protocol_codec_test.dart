import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_monitor/src/bridge_protocol_codec.dart';
import 'package:mobile_monitor/src/models.dart';
import 'package:mobile_monitor/src/profile_catalog.dart';

void main() {
  group('BridgeProtocolCodec', () {
    test('decodes device list payload', () {
      final devices = BridgeProtocolCodec.decodeDeviceList([
        2,
        0x34,
        0x12,
        1,
        1,
        0x00,
        0x01,
        0x78,
        0x56,
        0,
        1,
        0x00,
        0x02,
      ]);

      expect(devices, hasLength(2));
      expect(devices[0].addr, 0x1234);
      expect(devices[0].isOnline, isTrue);
      expect(devices[0].profileLoaded, isTrue);
      expect(devices[0].profileId, ProfileIds.airConditioner);
      expect(devices[1].addr, 0x5678);
      expect(devices[1].isOnline, isFalse);
      expect(devices[1].profileId, ProfileIds.light);
    });

    test('decodes state snapshot payload', () {
      final snapshots = BridgeProtocolCodec.decodeStateSnapshot([
        1,
        0x34,
        0x12,
        2,
        0x01,
        0x00,
        1,
        1,
        0,
        0,
        0,
        0x02,
        0x00,
        2,
        24,
        0,
        0,
        0,
      ]);

      expect(snapshots, hasLength(1));
      expect(snapshots.single.addr, 0x1234);
      expect(snapshots.single.states, hasLength(2));
      expect(snapshots.single.states[0].featureId, FeatureIds.power);
      expect(snapshots.single.states[0].type, FeatureType.boolValue);
      expect(snapshots.single.states[0].value, 1);
      expect(snapshots.single.states[1].featureId, FeatureIds.temperature);
      expect(snapshots.single.states[1].type, FeatureType.intValue);
      expect(snapshots.single.states[1].value, 24);
    });

    test('decodes feature notification payload', () {
      final notification = BridgeProtocolCodec.decodeNotification([
        0x34,
        0x12,
        0x04,
        0x00,
        3,
        2,
        0,
        0,
        0,
      ]);

      expect(notification.addr, 0x1234);
      expect(notification.featureId, FeatureIds.fanSpeed);
      expect(notification.type, FeatureType.enumValue);
      expect(notification.value, 2);
    });

    test('encodes set feature command', () {
      final command = BridgeProtocolCodec.encodeSetFeatureCommand(
        addr: 0x1234,
        featureId: FeatureIds.temperature,
        value: 26,
      );

      expect(command, [0x01, 0x34, 0x12, 0x02, 0x00, 26, 0, 0, 0]);
    });

    test('encodes group set feature command', () {
      final command = BridgeProtocolCodec.encodeGroupSetFeatureCommand(
        featureId: FeatureIds.power,
        value: 0,
      );

      expect(command, [0x02, 0x01, 0x00, 0, 0, 0, 0]);
    });

    test('encodes refresh all command', () {
      expect(BridgeProtocolCodec.encodeRefreshAllCommand(), [0x03]);
    });

    test('creates fallback feature for unknown profile state', () {
      const state = FeatureState(
        featureId: 0x9000,
        type: FeatureType.intValue,
        value: 42,
      );

      final feature = ProfileCatalog.fallbackFeature(state);

      expect(feature.id, 0x9000);
      expect(feature.name, 'Feature 0x9000');
      expect(feature.type, FeatureType.intValue);
      expect(feature.defaultValue, 42);
    });
  });
}
