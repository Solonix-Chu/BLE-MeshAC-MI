import 'dart:collection';

enum FeatureType {
  boolValue(1),
  intValue(2),
  enumValue(3),
  rawValue(4);

  const FeatureType(this.code);

  final int code;

  static FeatureType fromCode(int code) {
    return FeatureType.values.firstWhere(
      (type) => type.code == code,
      orElse: () => FeatureType.rawValue,
    );
  }
}

enum FeatureRole {
  auto,
  switchRole,
  temperature,
  mode,
  fanSpeed,
  brightness,
  position,
  volume,
  channel,
  mute,
  inputSource,
  analog,
}

class FeatureDefinition {
  const FeatureDefinition({
    required this.id,
    required this.name,
    required this.type,
    required this.role,
    this.min = 0,
    this.max = 1,
    this.step = 1,
    this.enumLabels = const [],
    this.defaultValue = 0,
    this.groupable = false,
  });

  final int id;
  final String name;
  final FeatureType type;
  final FeatureRole role;
  final int min;
  final int max;
  final int step;
  final List<String> enumLabels;
  final int defaultValue;
  final bool groupable;

  String formatValue(int value) {
    switch (role) {
      case FeatureRole.switchRole:
      case FeatureRole.mute:
        return value == 0 ? '关闭' : '开启';
      case FeatureRole.temperature:
        return '$value°C';
      case FeatureRole.brightness:
      case FeatureRole.position:
      case FeatureRole.volume:
        return '$value%';
      case FeatureRole.mode:
      case FeatureRole.fanSpeed:
      case FeatureRole.inputSource:
        if (value >= 0 && value < enumLabels.length) {
          return enumLabels[value];
        }
        return '$value';
      case FeatureRole.channel:
      case FeatureRole.analog:
      case FeatureRole.auto:
        return '$value';
    }
  }
}

class DeviceProfile {
  const DeviceProfile({
    required this.id,
    required this.name,
    required this.type,
    required this.features,
  });

  final int id;
  final String name;
  final String type;
  final List<FeatureDefinition> features;

  FeatureDefinition? featureById(int id) {
    for (final feature in features) {
      if (feature.id == id) {
        return feature;
      }
    }
    return null;
  }
}

class FeatureState {
  const FeatureState({
    required this.featureId,
    required this.type,
    required this.value,
  });

  final int featureId;
  final FeatureType type;
  final int value;

  FeatureState copyWith({FeatureType? type, int? value}) {
    return FeatureState(
      featureId: featureId,
      type: type ?? this.type,
      value: value ?? this.value,
    );
  }
}

class BridgeDeviceInfo {
  const BridgeDeviceInfo({
    required this.addr,
    required this.isOnline,
    required this.profileLoaded,
    required this.profileId,
  });

  final int addr;
  final bool isOnline;
  final bool profileLoaded;
  final int profileId;
}

class DeviceStateSnapshot {
  const DeviceStateSnapshot({required this.addr, required this.states});

  final int addr;
  final List<FeatureState> states;
}

class FeatureNotification {
  const FeatureNotification({
    required this.addr,
    required this.featureId,
    required this.type,
    required this.value,
  });

  final int addr;
  final int featureId;
  final FeatureType type;
  final int value;
}

class MonitorDevice {
  MonitorDevice({
    required this.addr,
    required this.isOnline,
    required this.profileLoaded,
    required this.profileId,
    Map<int, FeatureState>? states,
  }) : _states = Map<int, FeatureState>.unmodifiable(states ?? const {});

  factory MonitorDevice.fromInfo(BridgeDeviceInfo info) {
    return MonitorDevice(
      addr: info.addr,
      isOnline: info.isOnline,
      profileLoaded: info.profileLoaded,
      profileId: info.profileId,
    );
  }

  final int addr;
  final bool isOnline;
  final bool profileLoaded;
  final int profileId;
  final Map<int, FeatureState> _states;

  UnmodifiableMapView<int, FeatureState> get states {
    return UnmodifiableMapView(_states);
  }

  int? valueFor(int featureId) => _states[featureId]?.value;

  MonitorDevice copyWith({
    bool? isOnline,
    bool? profileLoaded,
    int? profileId,
    Map<int, FeatureState>? states,
  }) {
    return MonitorDevice(
      addr: addr,
      isOnline: isOnline ?? this.isOnline,
      profileLoaded: profileLoaded ?? this.profileLoaded,
      profileId: profileId ?? this.profileId,
      states: states ?? _states,
    );
  }

  MonitorDevice applyInfo(BridgeDeviceInfo info) {
    return copyWith(
      isOnline: info.isOnline,
      profileLoaded: info.profileLoaded,
      profileId: info.profileId,
    );
  }

  MonitorDevice applySnapshot(DeviceStateSnapshot snapshot) {
    final next = Map<int, FeatureState>.from(_states);
    for (final state in snapshot.states) {
      next[state.featureId] = state;
    }
    return copyWith(states: next);
  }

  MonitorDevice applyFeature(FeatureNotification notification) {
    final next = Map<int, FeatureState>.from(_states);
    next[notification.featureId] = FeatureState(
      featureId: notification.featureId,
      type: notification.type,
      value: notification.value,
    );
    return copyWith(states: next);
  }
}

class BridgeScanResult {
  const BridgeScanResult({
    required this.id,
    required this.name,
    required this.rssi,
    required this.serviceMatched,
  });

  final String id;
  final String name;
  final int rssi;
  final bool serviceMatched;
}

enum BridgeConnectionPhase {
  idle,
  scanning,
  connecting,
  connected,
  disconnected,
  error,
}

String hex16(int value) {
  return '0x${(value & 0xFFFF).toRadixString(16).padLeft(4, '0').toUpperCase()}';
}
