import 'package:flutter/material.dart';

import 'src/device_repository.dart';
import 'src/mesh_ac_ble_service.dart';
import 'src/models.dart';
import 'src/profile_catalog.dart';

void main() {
  runApp(const MeshMonitorApp());
}

class MeshMonitorApp extends StatefulWidget {
  const MeshMonitorApp({super.key, this.repository});

  final DeviceRepository? repository;

  @override
  State<MeshMonitorApp> createState() => _MeshMonitorAppState();
}

class _MeshMonitorAppState extends State<MeshMonitorApp> {
  late final DeviceRepository _repository;
  late final bool _ownsRepository;

  @override
  void initState() {
    super.initState();
    _ownsRepository = widget.repository == null;
    _repository = widget.repository ?? DeviceRepository(MeshAcBleService());
  }

  @override
  void dispose() {
    if (_ownsRepository) {
      _repository.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xFF167C80);
    return MaterialApp(
      title: 'MeshAC Monitor',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: seed,
          primary: seed,
          secondary: const Color(0xFFC7792F),
          tertiary: const Color(0xFF4269A8),
          surface: const Color(0xFFFAFBF9),
        ),
        scaffoldBackgroundColor: const Color(0xFFF4F6F3),
        cardTheme: const CardThemeData(
          elevation: 0,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.all(Radius.circular(8)),
          ),
          margin: EdgeInsets.zero,
        ),
      ),
      home: MonitorHomeScreen(repository: _repository),
    );
  }
}

class MonitorHomeScreen extends StatelessWidget {
  const MonitorHomeScreen({super.key, required this.repository});

  final DeviceRepository repository;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: repository,
      builder: (context, _) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('MeshAC 监控终端'),
            actions: [
              IconButton(
                tooltip: '刷新',
                onPressed: repository.isConnected && !repository.isBusy
                    ? () {
                        repository.refreshAll();
                      }
                    : null,
                icon: const Icon(Icons.refresh),
              ),
              IconButton(
                tooltip: '断开',
                onPressed: repository.isConnected && !repository.isBusy
                    ? () {
                        repository.disconnect();
                      }
                    : null,
                icon: const Icon(Icons.link_off),
              ),
            ],
          ),
          body: SafeArea(
            child: Stack(
              children: [
                RefreshIndicator(
                  onRefresh: repository.isConnected
                      ? repository.refreshAll
                      : repository.startScan,
                  child: ListView(
                    padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
                    children: [
                      ConnectionPanel(repository: repository),
                      const SizedBox(height: 12),
                      SummaryStrip(repository: repository),
                      const SizedBox(height: 12),
                      GroupControlPanel(repository: repository),
                      const SizedBox(height: 16),
                      DeviceList(repository: repository),
                    ],
                  ),
                ),
                if (repository.isBusy)
                  const Positioned(
                    left: 0,
                    right: 0,
                    top: 0,
                    child: LinearProgressIndicator(),
                  ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class ConnectionPanel extends StatelessWidget {
  const ConnectionPanel({super.key, required this.repository});

  final DeviceRepository repository;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final connected = repository.isConnected;
    final phaseLabel = switch (repository.phase) {
      BridgeConnectionPhase.idle => '待连接',
      BridgeConnectionPhase.scanning => '扫描中',
      BridgeConnectionPhase.connecting => '连接中',
      BridgeConnectionPhase.connected => '已连接',
      BridgeConnectionPhase.disconnected => '已断开',
      BridgeConnectionPhase.error => '异常',
    };

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                StatusDot(active: connected),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        connected
                            ? repository.connectedBridgeName ?? 'MeshAC Bridge'
                            : 'MeshAC Bridge',
                        style: Theme.of(context).textTheme.titleMedium,
                      ),
                      const SizedBox(height: 2),
                      Text(
                        phaseLabel,
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: scheme.onSurfaceVariant,
                        ),
                      ),
                    ],
                  ),
                ),
                FilledButton.icon(
                  onPressed: repository.isBusy
                      ? null
                      : () {
                          repository.startScan();
                        },
                  icon: const Icon(Icons.bluetooth_searching),
                  label: const Text('扫描'),
                ),
              ],
            ),
            if (repository.message != null) ...[
              const SizedBox(height: 12),
              Text(
                repository.message!,
                style: Theme.of(
                  context,
                ).textTheme.bodySmall?.copyWith(color: scheme.onSurfaceVariant),
              ),
            ],
            if (repository.scanResults.isNotEmpty && !connected) ...[
              const SizedBox(height: 14),
              ...repository.scanResults.map(
                (result) => BridgeResultTile(
                  result: result,
                  onConnect: repository.isBusy
                      ? null
                      : () {
                          repository.connect(result);
                        },
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class BridgeResultTile extends StatelessWidget {
  const BridgeResultTile({
    super.key,
    required this.result,
    required this.onConnect,
  });

  final BridgeScanResult result;
  final VoidCallback? onConnect;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      contentPadding: EdgeInsets.zero,
      leading: const Icon(Icons.settings_input_antenna),
      title: Text(result.name),
      subtitle: Text('${result.id} · RSSI ${result.rssi} dBm'),
      trailing: TextButton.icon(
        onPressed: onConnect,
        icon: const Icon(Icons.link),
        label: const Text('连接'),
      ),
    );
  }
}

class SummaryStrip extends StatelessWidget {
  const SummaryStrip({super.key, required this.repository});

  final DeviceRepository repository;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: MetricTile(
            icon: Icons.devices_other,
            label: '设备',
            value: '${repository.devices.length}',
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: MetricTile(
            icon: Icons.cloud_done_outlined,
            label: '在线',
            value: '${repository.onlineCount}',
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: MetricTile(
            icon: Icons.account_tree_outlined,
            label: 'Profile',
            value: '${_knownProfileCount(repository.devices)}',
          ),
        ),
      ],
    );
  }

  int _knownProfileCount(List<MonitorDevice> devices) {
    return devices
        .where((device) => ProfileCatalog.byId(device.profileId) != null)
        .length;
  }
}

class MetricTile extends StatelessWidget {
  const MetricTile({
    super.key,
    required this.icon,
    required this.label,
    required this.value,
  });

  final IconData icon;
  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Icon(icon, color: scheme.primary),
            const SizedBox(height: 12),
            Text(value, style: Theme.of(context).textTheme.headlineSmall),
            Text(
              label,
              style: Theme.of(
                context,
              ).textTheme.labelMedium?.copyWith(color: scheme.onSurfaceVariant),
            ),
          ],
        ),
      ),
    );
  }
}

class GroupControlPanel extends StatelessWidget {
  const GroupControlPanel({super.key, required this.repository});

  final DeviceRepository repository;

  @override
  Widget build(BuildContext context) {
    final enabled = repository.isConnected && !repository.isBusy;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.groups_2_outlined),
                const SizedBox(width: 10),
                Text('群组控制', style: Theme.of(context).textTheme.titleMedium),
              ],
            ),
            const SizedBox(height: 14),
            Wrap(
              spacing: 10,
              runSpacing: 10,
              children: [
                FilledButton.tonalIcon(
                  onPressed: enabled
                      ? () {
                          repository.setGroupFeature(
                            featureId: FeatureIds.power,
                            value: 1,
                            type: FeatureType.boolValue,
                          );
                        }
                      : null,
                  icon: const Icon(Icons.power_settings_new),
                  label: const Text('全开'),
                ),
                OutlinedButton.icon(
                  onPressed: enabled
                      ? () {
                          repository.setGroupFeature(
                            featureId: FeatureIds.power,
                            value: 0,
                            type: FeatureType.boolValue,
                          );
                        }
                      : null,
                  icon: const Icon(Icons.power_off),
                  label: const Text('全关'),
                ),
                OutlinedButton.icon(
                  onPressed: enabled
                      ? () {
                          repository.setGroupFeature(
                            featureId: FeatureIds.temperature,
                            value: 24,
                            type: FeatureType.intValue,
                          );
                        }
                      : null,
                  icon: const Icon(Icons.thermostat),
                  label: const Text('24°C'),
                ),
                OutlinedButton.icon(
                  onPressed: enabled
                      ? () {
                          repository.setGroupFeature(
                            featureId: FeatureIds.temperature,
                            value: 26,
                            type: FeatureType.intValue,
                          );
                        }
                      : null,
                  icon: const Icon(Icons.thermostat_auto),
                  label: const Text('26°C'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class DeviceList extends StatelessWidget {
  const DeviceList({super.key, required this.repository});

  final DeviceRepository repository;

  @override
  Widget build(BuildContext context) {
    if (repository.devices.isEmpty) {
      return Card(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            children: [
              Icon(
                Icons.sensors_off_outlined,
                size: 36,
                color: Theme.of(context).colorScheme.onSurfaceVariant,
              ),
              const SizedBox(height: 10),
              const Text('暂无设备状态'),
            ],
          ),
        ),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.only(bottom: 10),
          child: Text('设备列表', style: Theme.of(context).textTheme.titleMedium),
        ),
        ...repository.devices.map(
          (device) => Padding(
            padding: const EdgeInsets.only(bottom: 10),
            child: DeviceCard(repository: repository, device: device),
          ),
        ),
      ],
    );
  }
}

class DeviceCard extends StatelessWidget {
  const DeviceCard({super.key, required this.repository, required this.device});

  final DeviceRepository repository;
  final MonitorDevice device;

  @override
  Widget build(BuildContext context) {
    final profile = ProfileCatalog.byId(device.profileId);
    final power = device.valueFor(FeatureIds.power);
    final primaryValue = _primaryValue(profile, device);
    final scheme = Theme.of(context).colorScheme;

    return Card(
      child: InkWell(
        borderRadius: BorderRadius.circular(8),
        onTap: () {
          Navigator.of(context).push(
            MaterialPageRoute<void>(
              builder: (_) =>
                  DeviceDetailScreen(repository: repository, addr: device.addr),
            ),
          );
        },
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(
                    _profileIcon(profile),
                    color: device.isOnline
                        ? scheme.primary
                        : scheme.onSurfaceVariant,
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '${profile?.name ?? '未知设备'} ${hex16(device.addr)}',
                          style: Theme.of(context).textTheme.titleMedium,
                        ),
                        const SizedBox(height: 2),
                        Text(
                          'Profile ${hex16(device.profileId)}',
                          style: Theme.of(context).textTheme.bodySmall
                              ?.copyWith(color: scheme.onSurfaceVariant),
                        ),
                      ],
                    ),
                  ),
                  StatusChip(online: device.isOnline),
                ],
              ),
              const SizedBox(height: 14),
              Row(
                children: [
                  Expanded(
                    child: DeviceValue(
                      label: '电源',
                      value: power == null ? '-' : (power == 0 ? '关' : '开'),
                    ),
                  ),
                  Expanded(
                    child: DeviceValue(
                      label: primaryValue.$1,
                      value: primaryValue.$2,
                    ),
                  ),
                  IconButton(
                    tooltip: '详情',
                    onPressed: () {
                      Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => DeviceDetailScreen(
                            repository: repository,
                            addr: device.addr,
                          ),
                        ),
                      );
                    },
                    icon: const Icon(Icons.chevron_right),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  (String, String) _primaryValue(DeviceProfile? profile, MonitorDevice device) {
    if (profile == null) {
      return ('状态', '${device.states.length}');
    }
    final feature = profile.features.firstWhere(
      (item) =>
          item.role == FeatureRole.temperature ||
          item.role == FeatureRole.brightness ||
          item.role == FeatureRole.volume ||
          item.role == FeatureRole.position,
      orElse: () => profile.features.first,
    );
    final value = device.valueFor(feature.id) ?? feature.defaultValue;
    return (feature.name, feature.formatValue(value));
  }
}

class DeviceDetailScreen extends StatelessWidget {
  const DeviceDetailScreen({
    super.key,
    required this.repository,
    required this.addr,
  });

  final DeviceRepository repository;
  final int addr;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: repository,
      builder: (context, _) {
        final device = repository.deviceByAddr(addr);
        if (device == null) {
          return const Scaffold(body: Center(child: Text('设备不存在')));
        }
        final profile = ProfileCatalog.byId(device.profileId);
        final features = _featuresFor(device, profile);

        return Scaffold(
          appBar: AppBar(
            title: Text('${profile?.name ?? '未知设备'} ${hex16(device.addr)}'),
            actions: [
              Padding(
                padding: const EdgeInsets.only(right: 12),
                child: Center(child: StatusChip(online: device.isOnline)),
              ),
            ],
          ),
          body: SafeArea(
            child: ListView.separated(
              padding: const EdgeInsets.all(16),
              itemCount: features.length,
              separatorBuilder: (_, _) => const SizedBox(height: 10),
              itemBuilder: (context, index) {
                final feature = features[index];
                final state = device.states[feature.id];
                final value = state?.value ?? feature.defaultValue;
                return FeatureControl(
                  repository: repository,
                  device: device,
                  feature: feature,
                  value: value,
                );
              },
            ),
          ),
        );
      },
    );
  }

  List<FeatureDefinition> _featuresFor(
    MonitorDevice device,
    DeviceProfile? profile,
  ) {
    final features = <FeatureDefinition>[
      if (profile != null) ...profile.features,
    ];
    for (final state in device.states.values) {
      final known = features.any((feature) => feature.id == state.featureId);
      if (!known) {
        features.add(ProfileCatalog.fallbackFeature(state));
      }
    }
    return features;
  }
}

class FeatureControl extends StatelessWidget {
  const FeatureControl({
    super.key,
    required this.repository,
    required this.device,
    required this.feature,
    required this.value,
  });

  final DeviceRepository repository;
  final MonitorDevice device;
  final FeatureDefinition feature;
  final int value;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: switch (feature.type) {
        FeatureType.boolValue => SwitchListTile(
          secondary: Icon(_featureIcon(feature)),
          title: Text(feature.name),
          subtitle: Text(hex16(feature.id)),
          value: value != 0,
          onChanged: repository.isBusy || !device.isOnline
              ? null
              : (enabled) {
                  repository.setFeature(
                    addr: device.addr,
                    featureId: feature.id,
                    value: enabled ? 1 : 0,
                    type: feature.type,
                  );
                },
        ),
        FeatureType.enumValue => EnumFeatureControl(
          repository: repository,
          device: device,
          feature: feature,
          value: value,
        ),
        FeatureType.intValue => IntFeatureControl(
          repository: repository,
          device: device,
          feature: feature,
          value: value,
        ),
        FeatureType.rawValue => ListTile(
          leading: Icon(_featureIcon(feature)),
          title: Text(feature.name),
          subtitle: Text(hex16(feature.id)),
          trailing: Text('$value'),
        ),
      },
    );
  }
}

class EnumFeatureControl extends StatelessWidget {
  const EnumFeatureControl({
    super.key,
    required this.repository,
    required this.device,
    required this.feature,
    required this.value,
  });

  final DeviceRepository repository;
  final MonitorDevice device;
  final FeatureDefinition feature;
  final int value;

  @override
  Widget build(BuildContext context) {
    final labels = feature.enumLabels.isEmpty ? ['0'] : feature.enumLabels;
    final selected = value >= 0 && value < labels.length ? value : 0;

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(_featureIcon(feature)),
              const SizedBox(width: 10),
              Text(feature.name, style: Theme.of(context).textTheme.titleSmall),
              const Spacer(),
              Text(hex16(feature.id)),
            ],
          ),
          const SizedBox(height: 14),
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: SegmentedButton<int>(
              segments: [
                for (var i = 0; i < labels.length; i++)
                  ButtonSegment(value: i, label: Text(labels[i])),
              ],
              selected: {selected},
              onSelectionChanged: repository.isBusy || !device.isOnline
                  ? null
                  : (selection) {
                      repository.setFeature(
                        addr: device.addr,
                        featureId: feature.id,
                        value: selection.first,
                        type: feature.type,
                      );
                    },
            ),
          ),
        ],
      ),
    );
  }
}

class IntFeatureControl extends StatefulWidget {
  const IntFeatureControl({
    super.key,
    required this.repository,
    required this.device,
    required this.feature,
    required this.value,
  });

  final DeviceRepository repository;
  final MonitorDevice device;
  final FeatureDefinition feature;
  final int value;

  @override
  State<IntFeatureControl> createState() => _IntFeatureControlState();
}

class _IntFeatureControlState extends State<IntFeatureControl> {
  int? _draft;

  int get _value => _draft ?? widget.value;

  @override
  void didUpdateWidget(IntFeatureControl oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.value != widget.value) {
      _draft = null;
    }
  }

  @override
  Widget build(BuildContext context) {
    final feature = widget.feature;
    final enabled = !widget.repository.isBusy && widget.device.isOnline;
    final divisions = _sliderDivisions(feature);
    final clampedValue = _value.clamp(feature.min, feature.max);

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(_featureIcon(feature)),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  feature.name,
                  style: Theme.of(context).textTheme.titleSmall,
                ),
              ),
              Text(
                feature.formatValue(clampedValue),
                style: Theme.of(context).textTheme.titleMedium,
              ),
            ],
          ),
          const SizedBox(height: 12),
          Row(
            children: [
              IconButton.filledTonal(
                tooltip: '减少',
                onPressed: enabled ? () => _nudge(-feature.step) : null,
                icon: const Icon(Icons.remove),
              ),
              Expanded(
                child: Slider(
                  min: feature.min.toDouble(),
                  max: feature.max.toDouble(),
                  divisions: divisions,
                  value: clampedValue.toDouble(),
                  label: feature.formatValue(clampedValue),
                  onChanged: enabled
                      ? (value) => setState(() => _draft = _snap(value))
                      : null,
                  onChangeEnd: enabled
                      ? (value) => _sendValue(_snap(value))
                      : null,
                ),
              ),
              IconButton.filledTonal(
                tooltip: '增加',
                onPressed: enabled ? () => _nudge(feature.step) : null,
                icon: const Icon(Icons.add),
              ),
            ],
          ),
        ],
      ),
    );
  }

  int? _sliderDivisions(FeatureDefinition feature) {
    final count = ((feature.max - feature.min) / feature.step).round();
    return count > 0 && count <= 100 ? count : null;
  }

  int _snap(double raw) {
    final feature = widget.feature;
    final stepped =
        ((raw - feature.min) / feature.step).round() * feature.step +
        feature.min;
    return stepped.clamp(feature.min, feature.max);
  }

  void _nudge(int delta) {
    _sendValue((_value + delta).clamp(widget.feature.min, widget.feature.max));
  }

  void _sendValue(int value) {
    setState(() => _draft = value);
    widget.repository.setFeature(
      addr: widget.device.addr,
      featureId: widget.feature.id,
      value: value,
      type: widget.feature.type,
    );
  }
}

class StatusDot extends StatelessWidget {
  const StatusDot({super.key, required this.active});

  final bool active;

  @override
  Widget build(BuildContext context) {
    final color = active
        ? const Color(0xFF2F9E44)
        : Theme.of(context).colorScheme.outline;
    return Container(
      width: 12,
      height: 12,
      decoration: BoxDecoration(color: color, shape: BoxShape.circle),
    );
  }
}

class StatusChip extends StatelessWidget {
  const StatusChip({super.key, required this.online});

  final bool online;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return DecoratedBox(
      decoration: BoxDecoration(
        color: online
            ? scheme.primaryContainer
            : scheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(20),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
        child: Text(
          online ? '在线' : '离线',
          style: Theme.of(context).textTheme.labelSmall?.copyWith(
            color: online ? scheme.onPrimaryContainer : scheme.onSurfaceVariant,
          ),
        ),
      ),
    );
  }
}

class DeviceValue extends StatelessWidget {
  const DeviceValue({super.key, required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: Theme.of(context).textTheme.labelMedium?.copyWith(
            color: Theme.of(context).colorScheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 4),
        Text(value, style: Theme.of(context).textTheme.titleLarge),
      ],
    );
  }
}

IconData _profileIcon(DeviceProfile? profile) {
  return switch (profile?.id) {
    ProfileIds.airConditioner => Icons.ac_unit,
    ProfileIds.light => Icons.lightbulb_outline,
    ProfileIds.switchDevice => Icons.toggle_on_outlined,
    ProfileIds.tv => Icons.tv,
    ProfileIds.curtain => Icons.curtains,
    _ => Icons.memory,
  };
}

IconData _featureIcon(FeatureDefinition feature) {
  return switch (feature.role) {
    FeatureRole.switchRole => Icons.power_settings_new,
    FeatureRole.temperature => Icons.thermostat,
    FeatureRole.mode => Icons.tune,
    FeatureRole.fanSpeed => Icons.air,
    FeatureRole.brightness => Icons.light_mode,
    FeatureRole.position => Icons.open_in_full,
    FeatureRole.volume => Icons.volume_up,
    FeatureRole.channel => Icons.numbers,
    FeatureRole.mute => Icons.volume_off,
    FeatureRole.inputSource => Icons.input,
    FeatureRole.analog => Icons.speed,
    FeatureRole.auto => Icons.data_object,
  };
}
