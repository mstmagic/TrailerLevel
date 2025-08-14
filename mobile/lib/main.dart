import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:multicast_dns/multicast_dns.dart';

void main() {
  runApp(const TrailerLevelApp());
}

class TrailerLevelApp extends StatefulWidget {
  const TrailerLevelApp({super.key});
  @override
  State<TrailerLevelApp> createState() => _TrailerLevelAppState();
}

class _TrailerLevelAppState extends State<TrailerLevelApp> {
  String? deviceIp;
  double pitch = 0, roll = 0, heading = 0;
  double threshold = 10;
  final List<double> thresholds = [10, 5, 2.5, 1];
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    discoverDevice();
    _timer = Timer.periodic(const Duration(milliseconds: 500), (_) => fetchSensor());
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  Future<void> discoverDevice() async {
    final client = MDnsClient();
    await client.start();
    await for (final PtrResourceRecord ptr in client.lookup<PtrResourceRecord>(
        ResourceRecordQuery.serverPointer('_trailerlevel._tcp'))) {
      await for (final SrvResourceRecord srv in client.lookup<SrvResourceRecord>(
          ResourceRecordQuery.service(ptr.domainName))) {
        await for (final IPAddressResourceRecord ip in client.lookup<IPAddressResourceRecord>(
            ResourceRecordQuery.addressIPv4(srv.target))) {
          setState(() => deviceIp = ip.address.address);
        }
      }
    }
    client.stop();
  }

  Future<void> fetchSensor() async {
    if (deviceIp == null) return;
    try {
      final res = await http.get(Uri.parse('http://$deviceIp/sensor'));
      if (res.statusCode == 200) {
        final data = jsonDecode(res.body);
        setState(() {
          pitch = (data['pitch'] ?? 0).toDouble();
          roll = (data['roll'] ?? 0).toDouble();
          heading = (data['heading'] ?? 0).toDouble();
        });
      }
    } catch (_) {}
  }

  void cycleThreshold() {
    final i = thresholds.indexOf(threshold);
    setState(() {
      threshold = thresholds[(i + 1) % thresholds.length];
    });
  }

  void changeWifi() {
    if (deviceIp == null) return;
    showDialog(
      context: context,
      builder: (context) {
        final ssidController = TextEditingController();
        final passController = TextEditingController();
        return AlertDialog(
          title: const Text('Change WiFi'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(controller: ssidController, decoration: const InputDecoration(labelText: 'SSID')),
              TextField(controller: passController, decoration: const InputDecoration(labelText: 'Password')),
            ],
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(context), child: const Text('Cancel')),
            TextButton(
                onPressed: () async {
                  await http.post(Uri.parse('http://$deviceIp/wifi'),
                      headers: {'Content-Type': 'application/json'},
                      body: jsonEncode({'ssid': ssidController.text, 'password': passController.text}));
                  if (mounted) Navigator.pop(context);
                },
                child: const Text('Save')),
          ],
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Trailer Level',
      home: Scaffold(
        appBar: AppBar(title: const Text('Trailer Level')),
        body: Column(
          children: [
            Expanded(
              child: Center(
                child: LevelGauge(pitch: pitch, roll: roll, threshold: threshold),
              ),
            ),
            SizedBox(
              height: 160,
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: [
                  VerticalGauge(value: pitch, threshold: threshold),
                  HorizontalGauge(value: roll, threshold: threshold),
                  CompassGauge(heading: heading),
                ],
              ),
            ),
            TextButton(
              onPressed: cycleThreshold,
              child: Text('Threshold $threshold°'),
            ),
          ],
        ),
        floatingActionButton: FloatingActionButton(
          onPressed: changeWifi,
          child: const Icon(Icons.wifi),
        ),
      ),
    );
  }
}

class LevelGauge extends StatelessWidget {
  final double pitch;
  final double roll;
  final double threshold;
  const LevelGauge({super.key, required this.pitch, required this.roll, required this.threshold});

  @override
  Widget build(BuildContext context) {
    final activeUp = pitch < -1;
    final activeDown = pitch > 1;
    final activeLeft = roll > 1;
    final activeRight = roll < -1;
    return SizedBox(
      width: 200,
      height: 200,
      child: Stack(
        alignment: Alignment.center,
        children: [
          CustomPaint(
            size: const Size.square(200),
            painter: LevelGaugePainter(pitch, roll, threshold),
          ),
          Positioned(top: 0, child: Icon(Icons.arrow_drop_up, color: activeUp ? Colors.orange : Colors.grey)),
          Positioned(bottom: 0, child: Icon(Icons.arrow_drop_down, color: activeDown ? Colors.orange : Colors.grey)),
          Positioned(left: 0, child: Icon(Icons.arrow_left, color: activeLeft ? Colors.orange : Colors.grey)),
          Positioned(right: 0, child: Icon(Icons.arrow_right, color: activeRight ? Colors.orange : Colors.grey)),
        ],
      ),
    );
  }
}

class LevelGaugePainter extends CustomPainter {
  final double pitch, roll, threshold;
  LevelGaugePainter(this.pitch, this.roll, this.threshold);

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = size.width / 2;
    final line = Paint()
      ..color = Colors.black
      ..style = PaintingStyle.stroke;
    canvas.drawCircle(center, radius, line);
    canvas.drawLine(Offset(center.dx, center.dy - radius), Offset(center.dx, center.dy + radius), line);
    canvas.drawLine(Offset(center.dx - radius, center.dy), Offset(center.dx + radius, center.dy), line);

    final x = (roll / threshold).clamp(-1.0, 1.0) * radius;
    final y = (pitch / threshold).clamp(-1.0, 1.0) * radius;
    final dot = Paint()..color = Colors.red;
    canvas.drawCircle(center + Offset(x, y), 5, dot);
  }

  @override
  bool shouldRepaint(covariant LevelGaugePainter old) {
    return old.pitch != pitch || old.roll != roll || old.threshold != threshold;
  }
}

class VerticalGauge extends StatelessWidget {
  final double value;
  final double threshold;
  const VerticalGauge({super.key, required this.value, required this.threshold});

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      size: const Size(60, 150),
      painter: VerticalGaugePainter(value, threshold),
    );
  }
}

class VerticalGaugePainter extends CustomPainter {
  final double value, threshold;
  VerticalGaugePainter(this.value, this.threshold);

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = Colors.black
      ..strokeWidth = 2;
    final centerX = size.width / 2;
    canvas.drawLine(Offset(centerX, 0), Offset(centerX, size.height), paint);
    final tickSpacing = size.height / 8;
    final step = (2 * threshold) / 8;
    for (int i = 0; i < 9; i++) {
      final y = i * tickSpacing;
      canvas.drawLine(Offset(centerX - 10, y), Offset(centerX + 10, y), paint);
      final textPainter = TextPainter(
        text: TextSpan(
            text: (threshold - i * step).toStringAsFixed(1),
            style: const TextStyle(fontSize: 10, color: Colors.black)),
        textDirection: TextDirection.ltr,
      );
      textPainter.layout();
      textPainter.paint(canvas, Offset(0, y - 6));
    }
    final position = ((-value + threshold) / (2 * threshold)).clamp(0.0, 1.0) * size.height;
    canvas.drawCircle(Offset(centerX, position), 5, Paint()..color = Colors.red);
  }

  @override
  bool shouldRepaint(covariant VerticalGaugePainter old) {
    return old.value != value || old.threshold != threshold;
  }
}

class HorizontalGauge extends StatelessWidget {
  final double value;
  final double threshold;
  const HorizontalGauge({super.key, required this.value, required this.threshold});

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      size: const Size(150, 60),
      painter: HorizontalGaugePainter(value, threshold),
    );
  }
}

class HorizontalGaugePainter extends CustomPainter {
  final double value, threshold;
  HorizontalGaugePainter(this.value, this.threshold);

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = Colors.black
      ..strokeWidth = 2;
    final centerY = size.height / 2;
    canvas.drawLine(Offset(0, centerY), Offset(size.width, centerY), paint);
    final tickSpacing = size.width / 8;
    final step = (2 * threshold) / 8;
    for (int i = 0; i < 9; i++) {
      final x = i * tickSpacing;
      canvas.drawLine(Offset(x, centerY - 10), Offset(x, centerY + 10), paint);
      final textPainter = TextPainter(
        text: TextSpan(
            text: (-threshold + i * step).toStringAsFixed(1),
            style: const TextStyle(fontSize: 10, color: Colors.black)),
        textDirection: TextDirection.ltr,
      );
      textPainter.layout();
      textPainter.paint(canvas, Offset(x - 12, centerY + 12));
    }
    final position = ((value + threshold) / (2 * threshold)).clamp(0.0, 1.0) * size.width;
    canvas.drawCircle(Offset(position, centerY), 5, Paint()..color = Colors.red);
  }

  @override
  bool shouldRepaint(covariant HorizontalGaugePainter old) {
    return old.value != value || old.threshold != threshold;
  }
}

class CompassGauge extends StatelessWidget {
  final double heading;
  const CompassGauge({super.key, required this.heading});

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      size: const Size.square(100),
      painter: CompassPainter(heading),
    );
  }
}

class CompassPainter extends CustomPainter {
  final double heading;
  CompassPainter(this.heading);

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = size.width / 2;
    final paint = Paint()
      ..color = Colors.black
      ..style = PaintingStyle.stroke;
    canvas.drawCircle(center, radius, paint);
    final textPainter = TextPainter(
      text: const TextSpan(text: 'N', style: TextStyle(color: Colors.black)),
      textDirection: TextDirection.ltr,
    );
    textPainter.layout();
    textPainter.paint(canvas, Offset(center.dx - 5, center.dy - radius + 4));
    final angle = (heading - 90) * math.pi / 180;
    final arrowPaint = Paint()
      ..color = Colors.red
      ..strokeWidth = 2;
    final end = center + Offset(math.cos(angle), math.sin(angle)) * (radius - 10);
    canvas.drawLine(center, end, arrowPaint);
  }

  @override
  bool shouldRepaint(covariant CompassPainter old) {
    return old.heading != heading;
  }
}
