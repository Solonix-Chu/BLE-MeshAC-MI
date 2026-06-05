import 'models.dart';

abstract final class FeatureIds {
  static const power = 0x0001;
  static const temperature = 0x0002;
  static const mode = 0x0003;
  static const fanSpeed = 0x0004;
  static const brightness = 0x0005;
  static const volume = 0x0006;
  static const channel = 0x0007;
  static const mute = 0x0008;
  static const inputSource = 0x0009;
  static const position = 0x000A;
  static const openClose = 0x000B;
  static const analogValue = 0x000C;
}

abstract final class ProfileIds {
  static const airConditioner = 0x0100;
  static const light = 0x0200;
  static const switchDevice = 0x0300;
  static const tv = 0x0400;
  static const curtain = 0x0500;
}

abstract final class ProfileCatalog {
  static const airConditioner = DeviceProfile(
    id: ProfileIds.airConditioner,
    name: '空调',
    type: 'air_conditioner',
    features: [
      FeatureDefinition(
        id: FeatureIds.power,
        name: '电源',
        type: FeatureType.boolValue,
        role: FeatureRole.switchRole,
        min: 0,
        max: 1,
        defaultValue: 0,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.temperature,
        name: '温度',
        type: FeatureType.intValue,
        role: FeatureRole.temperature,
        min: 16,
        max: 30,
        step: 1,
        defaultValue: 25,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.mode,
        name: '模式',
        type: FeatureType.enumValue,
        role: FeatureRole.mode,
        enumLabels: ['制冷', '制热', '送风', '除湿', '自动'],
        defaultValue: 0,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.fanSpeed,
        name: '风速',
        type: FeatureType.enumValue,
        role: FeatureRole.fanSpeed,
        enumLabels: ['低', '中', '高'],
        defaultValue: 0,
        groupable: true,
      ),
    ],
  );

  static const light = DeviceProfile(
    id: ProfileIds.light,
    name: '灯光',
    type: 'light',
    features: [
      FeatureDefinition(
        id: FeatureIds.power,
        name: '电源',
        type: FeatureType.boolValue,
        role: FeatureRole.switchRole,
        min: 0,
        max: 1,
        defaultValue: 0,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.brightness,
        name: '亮度',
        type: FeatureType.intValue,
        role: FeatureRole.brightness,
        min: 0,
        max: 100,
        step: 5,
        defaultValue: 50,
        groupable: true,
      ),
    ],
  );

  static const switchDevice = DeviceProfile(
    id: ProfileIds.switchDevice,
    name: '开关',
    type: 'switch',
    features: [
      FeatureDefinition(
        id: FeatureIds.power,
        name: '电源',
        type: FeatureType.boolValue,
        role: FeatureRole.switchRole,
        min: 0,
        max: 1,
        defaultValue: 0,
        groupable: true,
      ),
    ],
  );

  static const tv = DeviceProfile(
    id: ProfileIds.tv,
    name: '电视',
    type: 'television',
    features: [
      FeatureDefinition(
        id: FeatureIds.power,
        name: '电源',
        type: FeatureType.boolValue,
        role: FeatureRole.switchRole,
        min: 0,
        max: 1,
        defaultValue: 0,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.volume,
        name: '音量',
        type: FeatureType.intValue,
        role: FeatureRole.volume,
        min: 0,
        max: 100,
        step: 5,
        defaultValue: 20,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.channel,
        name: '频道',
        type: FeatureType.intValue,
        role: FeatureRole.channel,
        min: 1,
        max: 999,
        step: 1,
        defaultValue: 1,
      ),
      FeatureDefinition(
        id: FeatureIds.mute,
        name: '静音',
        type: FeatureType.boolValue,
        role: FeatureRole.mute,
        min: 0,
        max: 1,
        defaultValue: 0,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.inputSource,
        name: '输入',
        type: FeatureType.enumValue,
        role: FeatureRole.inputSource,
        enumLabels: ['HDMI1', 'HDMI2', 'AV', 'Cast'],
        defaultValue: 0,
      ),
    ],
  );

  static const curtain = DeviceProfile(
    id: ProfileIds.curtain,
    name: '窗帘',
    type: 'curtain',
    features: [
      FeatureDefinition(
        id: FeatureIds.power,
        name: '电源',
        type: FeatureType.boolValue,
        role: FeatureRole.switchRole,
        min: 0,
        max: 1,
        defaultValue: 1,
        groupable: true,
      ),
      FeatureDefinition(
        id: FeatureIds.position,
        name: '位置',
        type: FeatureType.intValue,
        role: FeatureRole.position,
        min: 0,
        max: 100,
        step: 10,
        defaultValue: 0,
        groupable: true,
      ),
    ],
  );

  static const all = [airConditioner, light, switchDevice, tv, curtain];

  static DeviceProfile? byId(int profileId) {
    for (final profile in all) {
      if (profile.id == profileId) {
        return profile;
      }
    }
    return null;
  }

  static FeatureDefinition fallbackFeature(FeatureState state) {
    return FeatureDefinition(
      id: state.featureId,
      name: 'Feature ${hex16(state.featureId)}',
      type: state.type,
      role: _inferRole(state),
      min: 0,
      max: state.type == FeatureType.boolValue ? 1 : 100,
      step: 1,
      defaultValue: state.value,
    );
  }

  static FeatureRole _inferRole(FeatureState state) {
    return switch (state.type) {
      FeatureType.boolValue => FeatureRole.switchRole,
      FeatureType.enumValue => FeatureRole.mode,
      FeatureType.intValue => FeatureRole.analog,
      FeatureType.rawValue => FeatureRole.auto,
    };
  }
}
