package com.hno.tabletusbdiag;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.hardware.usb.UsbAccessory;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.method.ScrollingMovementMethod;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TimeZone;

public final class MainActivity extends Activity {
    private static final int CREATE_REPORT_REQUEST = 1001;

    private TextView resultView;
    private TextView stateView;
    private ProgressBar progressBar;
    private Button runButton;
    private Button saveButton;
    private Button shareButton;
    private Button copyButton;
    private volatile String latestReport = "";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildUi();
    }

    private void buildUi() {
        int pad = dp(16);
        int gap = dp(8);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("태블릿 USB Device/Gadget 진단");
        title.setTextSize(22f);
        title.setTextColor(0xFF102027);
        title.setGravity(Gravity.START);
        root.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView guide = new TextView(this);
        guide.setText("노트북과 태블릿을 USB 케이블로 연결한 상태에서 진단을 실행하세요.\n"
                + "이 앱은 설정을 변경하지 않고 읽을 수 있는 정보만 수집합니다.");
        guide.setTextSize(14f);
        guide.setTextColor(0xFF455A64);
        LinearLayout.LayoutParams guideParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        guideParams.topMargin = gap;
        root.addView(guide, guideParams);

        stateView = new TextView(this);
        stateView.setText("대기 중");
        stateView.setTextSize(15f);
        stateView.setTextColor(0xFF263238);
        LinearLayout.LayoutParams stateParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        stateParams.topMargin = dp(12);
        root.addView(stateView, stateParams);

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setIndeterminate(true);
        progressBar.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                dp(8));
        progressParams.topMargin = gap;
        root.addView(progressBar, progressParams);

        LinearLayout buttonRow1 = new LinearLayout(this);
        buttonRow1.setOrientation(LinearLayout.HORIZONTAL);
        buttonRow1.setGravity(Gravity.START);

        runButton = new Button(this);
        runButton.setText("진단 실행");
        runButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                runDiagnostic();
            }
        });
        buttonRow1.addView(runButton, weightedButtonParams());

        saveButton = new Button(this);
        saveButton.setText("TXT 저장");
        saveButton.setEnabled(false);
        saveButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                requestSaveReport();
            }
        });
        LinearLayout.LayoutParams saveParams = weightedButtonParams();
        saveParams.leftMargin = gap;
        buttonRow1.addView(saveButton, saveParams);

        LinearLayout.LayoutParams row1Params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        row1Params.topMargin = dp(12);
        root.addView(buttonRow1, row1Params);

        LinearLayout buttonRow2 = new LinearLayout(this);
        buttonRow2.setOrientation(LinearLayout.HORIZONTAL);

        shareButton = new Button(this);
        shareButton.setText("결과 공유");
        shareButton.setEnabled(false);
        shareButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                shareReport();
            }
        });
        buttonRow2.addView(shareButton, weightedButtonParams());

        copyButton = new Button(this);
        copyButton.setText("전체 복사");
        copyButton.setEnabled(false);
        copyButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                copyReport();
            }
        });
        LinearLayout.LayoutParams copyParams = weightedButtonParams();
        copyParams.leftMargin = gap;
        buttonRow2.addView(copyButton, copyParams);

        LinearLayout.LayoutParams row2Params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        row2Params.topMargin = gap;
        root.addView(buttonRow2, row2Params);

        resultView = new TextView(this);
        resultView.setText("진단 결과가 여기에 표시됩니다.");
        resultView.setTextSize(12f);
        resultView.setTextColor(0xFF212121);
        resultView.setTextIsSelectable(true);
        resultView.setTypeface(android.graphics.Typeface.MONOSPACE);
        resultView.setMovementMethod(new ScrollingMovementMethod());
        resultView.setPadding(dp(10), dp(10), dp(10), dp(10));
        resultView.setBackgroundColor(0xFFF5F5F5);

        ScrollView scrollView = new ScrollView(this);
        scrollView.addView(resultView, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                0,
                1f);
        scrollParams.topMargin = dp(12);
        root.addView(scrollView, scrollParams);

        setContentView(root);
    }

    private LinearLayout.LayoutParams weightedButtonParams() {
        return new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void runDiagnostic() {
        runButton.setEnabled(false);
        saveButton.setEnabled(false);
        shareButton.setEnabled(false);
        copyButton.setEnabled(false);
        progressBar.setVisibility(View.VISIBLE);
        stateView.setText("진단 중… 약 5~20초 걸릴 수 있습니다.");
        resultView.setText("정보 수집 중입니다.\nUSB 케이블을 연결한 상태를 유지하세요.");

        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                String report;
                try {
                    report = DiagnosticEngine.collect(MainActivity.this);
                } catch (Throwable t) {
                    report = "진단 앱 내부 오류\n\n" + stackTraceToString(t);
                }
                final String completed = report;
                latestReport = completed;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        progressBar.setVisibility(View.GONE);
                        stateView.setText("진단 완료 — TXT로 저장한 뒤 대화에 업로드하세요.");
                        resultView.setText(completed);
                        runButton.setEnabled(true);
                        saveButton.setEnabled(true);
                        shareButton.setEnabled(true);
                        copyButton.setEnabled(true);
                    }
                });
            }
        }, "tablet-usb-diagnostic");
        worker.start();
    }

    private void requestSaveReport() {
        if (latestReport.isEmpty()) {
            Toast.makeText(this, "먼저 진단을 실행하세요.", Toast.LENGTH_SHORT).show();
            return;
        }
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");
        String timestamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        intent.putExtra(Intent.EXTRA_TITLE, "Tablet_USB_Diagnostic_" + timestamp + ".txt");
        startActivityForResult(intent, CREATE_REPORT_REQUEST);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != CREATE_REPORT_REQUEST || resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        try (OutputStream output = getContentResolver().openOutputStream(data.getData(), "w")) {
            if (output == null) {
                throw new IOException("출력 스트림을 열 수 없습니다.");
            }
            output.write(latestReport.getBytes(StandardCharsets.UTF_8));
            output.flush();
            Toast.makeText(this, "TXT 저장 완료", Toast.LENGTH_LONG).show();
        } catch (Exception e) {
            Toast.makeText(this, "저장 실패: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void shareReport() {
        if (latestReport.isEmpty()) {
            return;
        }
        Intent share = new Intent(Intent.ACTION_SEND);
        share.setType("text/plain");
        share.putExtra(Intent.EXTRA_SUBJECT, "태블릿 USB 진단 결과");
        share.putExtra(Intent.EXTRA_TEXT, latestReport);
        startActivity(Intent.createChooser(share, "진단 결과 공유"));
    }

    private void copyReport() {
        if (latestReport.isEmpty()) {
            return;
        }
        ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        if (clipboard != null) {
            clipboard.setPrimaryClip(ClipData.newPlainText("Tablet USB diagnostic", latestReport));
            Toast.makeText(this, "전체 결과를 복사했습니다.", Toast.LENGTH_SHORT).show();
        }
    }

    private static String stackTraceToString(Throwable throwable) {
        StringBuilder out = new StringBuilder();
        out.append(throwable).append('\n');
        for (StackTraceElement element : throwable.getStackTrace()) {
            out.append("    at ").append(element).append('\n');
        }
        return out.toString();
    }

    private static final class DiagnosticEngine {
        private static final String USB_STATE_ACTION = "android.hardware.usb.action.USB_STATE";
        private static final int MAX_COMMAND_CHARS = 120_000;
        private static final int MAX_SYSFS_ENTRIES = 450;
        private static final int MAX_FILE_BYTES = 4096;

        private final Context context;
        private final StringBuilder report = new StringBuilder(64_000);
        private final Facts facts = new Facts();

        private DiagnosticEngine(Context context) {
            this.context = context.getApplicationContext();
        }

        static String collect(Context context) {
            DiagnosticEngine engine = new DiagnosticEngine(context);
            engine.collectAll();
            return engine.report.toString();
        }

        private void collectAll() {
            header();
            buildInfo();
            packageFeatures();
            developerAndUsbSettings();
            usbStateBroadcast();
            usbManagerState();
            relevantSystemProperties();
            networkState();
            commandChecks();
            sysfsChecks();
            deviceNodeChecks();
            verdict();
            privacyNote();
        }

        private void header() {
            section("0. 실행 정보");
            line("앱: HNO Tablet USB Diagnostic 1.0.0");
            line("실행 시각: " + isoTime(new Date()));
            line("주의: 이 앱은 USB 역할이나 시스템 설정을 변경하지 않습니다.");
            line("권장 상태: 태블릿과 노트북을 USB 케이블로 연결한 상태에서 실행");
        }

        private void buildInfo() {
            section("1. Android / 보드 / SoC 정보");
            kv("Android release", Build.VERSION.RELEASE);
            kv("SDK_INT", String.valueOf(Build.VERSION.SDK_INT));
            kv("Security patch", Build.VERSION.SECURITY_PATCH);
            kv("Manufacturer", Build.MANUFACTURER);
            kv("Brand", Build.BRAND);
            kv("Model", Build.MODEL);
            kv("Product", Build.PRODUCT);
            kv("Device", Build.DEVICE);
            kv("Board", Build.BOARD);
            kv("Hardware", Build.HARDWARE);
            kv("Bootloader", Build.BOOTLOADER);
            kv("Display", Build.DISPLAY);
            kv("Build ID", Build.ID);
            kv("Build type", Build.TYPE);
            kv("Build tags", Build.TAGS);
            kv("Fingerprint", Build.FINGERPRINT);
            kv("Supported ABIs", Arrays.toString(Build.SUPPORTED_ABIS));
            kv("Kernel /proc/version", readSingleFile("/proc/version", 8192));
            kv("Kernel cmdline /proc/cmdline", readSingleFile("/proc/cmdline", 8192));
        }

        private void packageFeatures() {
            section("2. Android 하드웨어 기능 선언");
            PackageManager pm = context.getPackageManager();
            facts.usbHostFeature = pm.hasSystemFeature(PackageManager.FEATURE_USB_HOST);
            facts.usbAccessoryFeature = pm.hasSystemFeature("android.hardware.usb.accessory");
            feature("android.hardware.usb.host", facts.usbHostFeature);
            feature("android.hardware.usb.accessory", facts.usbAccessoryFeature);
            feature("android.hardware.wifi", pm.hasSystemFeature(PackageManager.FEATURE_WIFI));
            feature("android.hardware.ethernet", pm.hasSystemFeature("android.hardware.ethernet"));
            feature("android.hardware.touchscreen", pm.hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN));
            feature("android.software.leanback", pm.hasSystemFeature(PackageManager.FEATURE_LEANBACK));
        }

        private void developerAndUsbSettings() {
            section("3. 개발자 옵션 / ADB 관련 Settings 값");
            int dev = readGlobalInt("development_settings_enabled");
            int adb = readGlobalInt("adb_enabled");
            int adbWifi = readGlobalInt("adb_wifi_enabled");
            facts.adbEnabledSetting = adb;
            kv("development_settings_enabled", settingValue(dev));
            kv("adb_enabled", settingValue(adb));
            kv("adb_wifi_enabled", settingValue(adbWifi));
            kv("adb_notify", settingValue(readGlobalInt("adb_notify")));
            kv("device_provisioned", settingValue(readGlobalInt("device_provisioned")));
        }

        private void usbStateBroadcast() {
            section("4. 현재 USB 상태 브로드캐스트");
            try {
                Intent sticky = context.registerReceiver(null, new IntentFilter(USB_STATE_ACTION));
                if (sticky == null) {
                    line("USB_STATE sticky broadcast: 없음");
                    facts.usbStateAvailable = false;
                    return;
                }
                facts.usbStateAvailable = true;
                Bundle extras = sticky.getExtras();
                if (extras == null || extras.isEmpty()) {
                    line("USB_STATE extras: 비어 있음");
                    return;
                }
                ArrayList<String> keys = new ArrayList<>(extras.keySet());
                Collections.sort(keys);
                for (String key : keys) {
                    Object value = extras.get(key);
                    kv(key, String.valueOf(value));
                    facts.usbState.put(key, String.valueOf(value));
                }
            } catch (Throwable t) {
                line("USB_STATE 읽기 실패: " + describe(t));
            }
        }

        private void usbManagerState() {
            section("5. UsbManager 장치/액세서리 상태");
            UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
            if (manager == null) {
                line("UsbManager 서비스 없음");
                return;
            }

            try {
                HashMap<String, UsbDevice> devices = manager.getDeviceList();
                facts.attachedUsbDeviceCount = devices.size();
                kv("연결된 USB 장치 수", String.valueOf(devices.size()));
                ArrayList<String> names = new ArrayList<>(devices.keySet());
                Collections.sort(names);
                for (String name : names) {
                    UsbDevice device = devices.get(name);
                    if (device != null) {
                        appendUsbDevice(manager, device);
                    }
                }
            } catch (Throwable t) {
                line("USB device list 실패: " + describe(t));
            }

            try {
                UsbAccessory[] accessories = manager.getAccessoryList();
                int count = accessories == null ? 0 : accessories.length;
                kv("연결된 USB 액세서리 수", String.valueOf(count));
                if (accessories != null) {
                    for (int i = 0; i < accessories.length; i++) {
                        UsbAccessory a = accessories[i];
                        line("Accessory[" + i + "]: " + a);
                    }
                }
            } catch (Throwable t) {
                line("USB accessory list 실패: " + describe(t));
            }
        }

        private void appendUsbDevice(UsbManager manager, UsbDevice device) {
            line("");
            line("USB Device: " + device.getDeviceName());
            kv("  VID:PID", hex(device.getVendorId(), 4) + ":" + hex(device.getProductId(), 4));
            kv("  Class/Subclass/Protocol", device.getDeviceClass() + "/" + device.getDeviceSubclass() + "/" + device.getDeviceProtocol());
            kv("  Configurations", String.valueOf(device.getConfigurationCount()));
            kv("  Interfaces", String.valueOf(device.getInterfaceCount()));
            kv("  Permission", String.valueOf(manager.hasPermission(device)));
            try {
                kv("  Manufacturer", String.valueOf(device.getManufacturerName()));
                kv("  Product", String.valueOf(device.getProductName()));
                kv("  Version", String.valueOf(device.getVersion()));
                kv("  Serial", String.valueOf(device.getSerialNumber()));
            } catch (Throwable t) {
                kv("  Descriptor strings", "읽기 제한: " + describe(t));
            }
            for (int i = 0; i < device.getInterfaceCount(); i++) {
                UsbInterface intf = device.getInterface(i);
                line("  Interface[" + i + "] id=" + intf.getId()
                        + " class=" + intf.getInterfaceClass()
                        + " subclass=" + intf.getInterfaceSubclass()
                        + " protocol=" + intf.getInterfaceProtocol()
                        + " endpoints=" + intf.getEndpointCount());
                for (int e = 0; e < intf.getEndpointCount(); e++) {
                    UsbEndpoint ep = intf.getEndpoint(e);
                    line("    Endpoint[" + e + "] address=" + ep.getAddress()
                            + " direction=" + ep.getDirection()
                            + " type=" + ep.getType()
                            + " maxPacket=" + ep.getMaxPacketSize());
                }
            }
            UsbDeviceConnection connection = null;
            if (manager.hasPermission(device)) {
                try {
                    connection = manager.openDevice(device);
                    kv("  openDevice", connection == null ? "null" : "성공");
                } catch (Throwable t) {
                    kv("  openDevice", "실패: " + describe(t));
                } finally {
                    if (connection != null) {
                        connection.close();
                    }
                }
            }
        }

        private void relevantSystemProperties() {
            section("6. USB/ADB 관련 시스템 속성(getprop)");
            String all = runCommand("getprop", "/system/bin/getprop");
            if (all.startsWith("ERROR:")) {
                line(all);
                return;
            }
            String[] lines = all.split("\\r?\\n");
            int matched = 0;
            for (String line : lines) {
                String lower = line.toLowerCase(Locale.US);
                if (isRelevantPropertyLine(lower)) {
                    this.line(line);
                    matched++;
                    rememberProperty(line);
                }
            }
            if (matched == 0) {
                line("관련 속성을 찾지 못했습니다.");
            }
        }

        private boolean isRelevantPropertyLine(String lower) {
            return lower.contains("usb")
                    || lower.contains("adb")
                    || lower.contains("ro.hardware")
                    || lower.contains("ro.board")
                    || lower.contains("ro.product")
                    || lower.contains("ro.bootmode")
                    || lower.contains("ro.boot.hardware")
                    || lower.contains("ro.debuggable")
                    || lower.contains("ro.secure")
                    || lower.contains("ro.build.type")
                    || lower.contains("ro.build.tags");
        }

        private void rememberProperty(String line) {
            int keyStart = line.indexOf('[');
            int keyEnd = line.indexOf(']');
            int valueStart = line.indexOf('[', keyEnd + 1);
            int valueEnd = line.lastIndexOf(']');
            if (keyStart >= 0 && keyEnd > keyStart && valueStart > keyEnd && valueEnd > valueStart) {
                String key = line.substring(keyStart + 1, keyEnd);
                String value = line.substring(valueStart + 1, valueEnd);
                facts.properties.put(key, value);
            }
        }

        private void networkState() {
            section("7. 네트워크 및 IP 정보");
            try {
                Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
                if (interfaces == null) {
                    line("NetworkInterface 목록 없음");
                } else {
                    List<NetworkInterface> sorted = Collections.list(interfaces);
                    Collections.sort(sorted, new Comparator<NetworkInterface>() {
                        @Override
                        public int compare(NetworkInterface a, NetworkInterface b) {
                            return a.getName().compareTo(b.getName());
                        }
                    });
                    for (NetworkInterface ni : sorted) {
                        line("");
                        line("Interface: " + ni.getName() + " (" + ni.getDisplayName() + ")");
                        try {
                            kv("  up", String.valueOf(ni.isUp()));
                            kv("  loopback", String.valueOf(ni.isLoopback()));
                            kv("  pointToPoint", String.valueOf(ni.isPointToPoint()));
                        } catch (Throwable t) {
                            kv("  flags", "읽기 실패: " + describe(t));
                        }
                        Enumeration<InetAddress> addresses = ni.getInetAddresses();
                        while (addresses.hasMoreElements()) {
                            InetAddress address = addresses.nextElement();
                            kv("  address", address.getHostAddress());
                        }
                    }
                }
            } catch (Throwable t) {
                line("NetworkInterface 실패: " + describe(t));
            }

            try {
                ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
                if (cm != null) {
                    if (Build.VERSION.SDK_INT >= 23) {
                        Network active = cm.getActiveNetwork();
                        NetworkCapabilities caps = active == null ? null : cm.getNetworkCapabilities(active);
                        kv("Active network", String.valueOf(active));
                        kv("Capabilities", String.valueOf(caps));
                    } else {
                        NetworkInfo info = cm.getActiveNetworkInfo();
                        kv("Active network", String.valueOf(info));
                    }
                }
            } catch (Throwable t) {
                line("ConnectivityManager 실패: " + describe(t));
            }

            try {
                WifiManager wifi = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                if (wifi != null) {
                    WifiInfo info = wifi.getConnectionInfo();
                    kv("Wi-Fi enabled", String.valueOf(wifi.isWifiEnabled()));
                    if (info != null) {
                        kv("Wi-Fi IP", ipv4FromInt(info.getIpAddress()));
                        kv("Wi-Fi network id", String.valueOf(info.getNetworkId()));
                    }
                }
            } catch (Throwable t) {
                line("WifiManager 제한: " + describe(t));
            }
        }

        private void commandChecks() {
            section("8. 셸 명령 기반 확인");
            commandBlock("id", "id");
            commandBlock("uname -a", "uname", "-a");
            commandBlock("SELinux getenforce", "/system/bin/getenforce");
            commandBlock("ip addr", "/system/bin/ip", "addr");
            commandBlock("ip route", "/system/bin/ip", "route");
            commandBlock("mount", "/system/bin/mount");
            commandBlock("su 위치", "/system/bin/sh", "-c", "command -v su; ls -l /system/bin/su /system/xbin/su /sbin/su 2>/dev/null");
        }

        private void sysfsChecks() {
            section("9. USB UDC / Role / Gadget sysfs 직접 확인");
            String[] roots = new String[] {
                    "/sys/class/udc",
                    "/sys/class/usb_role",
                    "/sys/class/dual_role_usb",
                    "/sys/class/typec",
                    "/sys/class/android_usb",
                    "/sys/kernel/config/usb_gadget",
                    "/config/usb_gadget"
            };
            for (String root : roots) {
                line("");
                line("### " + root);
                ProbeResult probe = inspectPath(root, 3, MAX_SYSFS_ENTRIES);
                facts.probes.put(root, probe);
                line("exists=" + probe.exists
                        + " directory=" + probe.directory
                        + " canRead=" + probe.canRead
                        + " immediateEntries=" + probe.immediateEntries
                        + " walkedEntries=" + probe.walkedEntries);
                if (probe.error != null && !probe.error.isEmpty()) {
                    line("error=" + probe.error);
                }
                report.append(probe.details);
            }
        }

        private void deviceNodeChecks() {
            section("10. USB/Gadget 관련 /dev 노드 후보");
            File dev = new File("/dev");
            File[] children;
            try {
                children = dev.listFiles();
            } catch (Throwable t) {
                children = null;
                line("/dev 읽기 실패: " + describe(t));
            }
            if (children == null) {
                line("/dev 목록 접근 불가 또는 비어 있음");
                return;
            }
            Arrays.sort(children, new Comparator<File>() {
                @Override
                public int compare(File a, File b) {
                    return a.getName().compareTo(b.getName());
                }
            });
            int count = 0;
            for (File child : children) {
                String lower = child.getName().toLowerCase(Locale.US);
                if (lower.contains("usb") || lower.contains("android") || lower.contains("mtp")
                        || lower.contains("gadget") || lower.contains("dwc") || lower.contains("musb")) {
                    line(child.getAbsolutePath() + " read=" + child.canRead() + " write=" + child.canWrite());
                    count++;
                }
            }
            if (count == 0) {
                line("이름 기준 후보 노드 없음");
            }
        }

        private void verdict() {
            section("11. 자동 판정");
            ProbeResult udc = facts.probes.get("/sys/class/udc");
            ProbeResult role = facts.probes.get("/sys/class/usb_role");
            ProbeResult dualRole = facts.probes.get("/sys/class/dual_role_usb");
            ProbeResult gadgetA = facts.probes.get("/sys/kernel/config/usb_gadget");
            ProbeResult gadgetB = facts.probes.get("/config/usb_gadget");

            line("[확인된 기능]");
            line("- Android USB Host 기능 선언: " + yesNo(facts.usbHostFeature));
            line("- Android USB Accessory 기능 선언: " + yesNo(facts.usbAccessoryFeature));
            line("- 현재 Host로 붙은 주변 USB 장치 수: " + facts.attachedUsbDeviceCount);
            line("- 개발자 설정의 adb_enabled: " + settingValue(facts.adbEnabledSetting));

            line("");
            line("[UDC 판정]");
            if (udc == null) {
                line("- /sys/class/udc 검사 결과가 없습니다.");
            } else if (udc.exists && udc.canRead && udc.immediateEntries > 0) {
                line("- UDC 컨트롤러 항목이 존재합니다.");
                line("- 의미: SoC/커널에 USB Device(Gadget) 컨트롤러가 등록된 상태입니다.");
                line("- Windows에 열거되지 않는 원인은 역할 전환, Gadget 바인딩, USB HAL 또는 포트 배선 쪽으로 좁혀집니다.");
            } else if (udc.exists && udc.canRead && udc.immediateEntries == 0) {
                line("- /sys/class/udc 디렉터리는 있으나 등록된 UDC가 0개입니다.");
                line("- 의미: 현재 커널/Device Tree에서 USB Device Controller 드라이버가 활성화되지 않았을 가능성이 큽니다.");
            } else if (!udc.exists) {
                line("- /sys/class/udc 경로가 없습니다.");
                line("- 의미: UDC 미지원 커널이거나 제조사가 해당 인터페이스를 제거한 가능성이 있습니다.");
            } else {
                line("- /sys/class/udc는 있으나 일반 앱 권한으로 읽지 못했습니다.");
                line("- 이 경우 UDC 존재 여부를 확정하려면 root/제조사 셸 또는 펌웨어 자료가 필요합니다.");
            }

            line("");
            line("[Role/Gadget 판정]");
            appendProbeSummary("USB role", role);
            appendProbeSummary("Dual-role", dualRole);
            appendProbeSummary("ConfigFS gadget #1", gadgetA);
            appendProbeSummary("ConfigFS gadget #2", gadgetB);

            String sysConfig = firstProperty("sys.usb.config", "persist.sys.usb.config", "vendor.usb.config");
            String sysState = firstProperty("sys.usb.state", "vendor.usb.state");
            kv("sys/persist USB config", blankAsUnknown(sysConfig));
            kv("sys/vendor USB state", blankAsUnknown(sysState));

            String connected = facts.usbState.get("connected");
            String configured = facts.usbState.get("configured");
            kv("USB_STATE connected", blankAsUnknown(connected));
            kv("USB_STATE configured", blankAsUnknown(configured));

            line("");
            line("[현재 결과 해석]");
            if (udc != null && udc.exists && udc.canRead && udc.immediateEntries > 0) {
                if (isFalse(connected) || isFalse(configured)) {
                    line("- Device 컨트롤러는 보이지만 노트북 연결 상태가 configured로 올라오지 않았습니다.");
                    line("- 우선순위: 포트 역할/CC·ID 배선 → USB Gadget UDC 바인딩 → 제조사 USB HAL 순서로 확인하십시오.");
                } else {
                    line("- Android 내부에서는 USB Device 연결이 인식된 흔적이 있습니다.");
                    line("- Windows 미인식이라면 descriptor/케이블/드라이버 이벤트를 다시 비교해야 합니다.");
                }
            } else if (udc != null && udc.exists && udc.canRead && udc.immediateEntries == 0) {
                line("- 사용자 설정이나 ADB 드라이버 설치만으로 해결될 가능성은 낮습니다.");
                line("- 제조사에 UDC 활성 Device Tree/커널과 USB Gadget 지원 펌웨어를 요청하는 것이 핵심입니다.");
            } else {
                line("- 일반 APK 권한만으로 UDC를 확정하지 못했습니다.");
                line("- role/configfs 값이 함께 보이지 않는다면 제조사 펌웨어 또는 root 진단이 필요합니다.");
            }

            line("");
            line("이 앱은 역할을 강제로 변경하지 않았습니다. 강제 전환은 UDC 존재가 확인된 뒤 root 권한 또는 제조사 펌웨어 수준에서만 검토해야 합니다.");
        }

        private void appendProbeSummary(String label, ProbeResult probe) {
            if (probe == null) {
                line("- " + label + ": 결과 없음");
                return;
            }
            line("- " + label + ": exists=" + probe.exists
                    + ", readable=" + probe.canRead
                    + ", immediateEntries=" + probe.immediateEntries);
        }

        private void privacyNote() {
            section("12. 전달 안내");
            line("1) 앱의 'TXT 저장'을 눌러 결과 파일을 저장합니다.");
            line("2) 그 TXT를 ChatGPT 대화에 업로드합니다.");
            line("3) 결과에는 로컬 IP, 빌드 fingerprint, USB 장치 descriptor가 포함될 수 있습니다.");
        }

        private ProbeResult inspectPath(String path, int maxDepth, int maxEntries) {
            ProbeResult result = new ProbeResult();
            File root = new File(path);
            result.exists = root.exists();
            result.directory = root.isDirectory();
            result.canRead = root.canRead();
            if (!result.exists) {
                return result;
            }

            try {
                File[] immediate = root.listFiles();
                result.immediateEntries = immediate == null ? -1 : immediate.length;
                if (immediate == null && root.isDirectory()) {
                    result.error = "listFiles()가 null을 반환함: 권한 제한 가능";
                    return result;
                }
            } catch (Throwable t) {
                result.immediateEntries = -1;
                result.error = describe(t);
                return result;
            }

            Set<String> visited = new HashSet<>();
            walk(root, 0, maxDepth, maxEntries, result, visited);
            return result;
        }

        private void walk(File file, int depth, int maxDepth, int maxEntries,
                          ProbeResult result, Set<String> visited) {
            if (result.walkedEntries >= maxEntries || depth > maxDepth) {
                return;
            }
            String canonical;
            try {
                canonical = file.getCanonicalPath();
            } catch (IOException e) {
                canonical = file.getAbsolutePath();
            }
            if (!visited.add(canonical)) {
                return;
            }

            String indent = repeat("  ", depth);
            result.details.append(indent)
                    .append(file.getAbsolutePath())
                    .append(" type=").append(file.isDirectory() ? "dir" : "file")
                    .append(" read=").append(file.canRead())
                    .append(" write=").append(file.canWrite())
                    .append(" canonical=").append(canonical)
                    .append('\n');
            result.walkedEntries++;

            if (file.isFile()) {
                String content = readSingleFile(file.getAbsolutePath(), MAX_FILE_BYTES);
                if (!content.isEmpty()) {
                    result.details.append(indent).append("  value=")
                            .append(content.replace("\n", "\\n"))
                            .append('\n');
                }
                return;
            }

            if (!file.isDirectory() || depth >= maxDepth) {
                return;
            }

            File[] children;
            try {
                children = file.listFiles();
            } catch (Throwable t) {
                result.details.append(indent).append("  list error=").append(describe(t)).append('\n');
                return;
            }
            if (children == null) {
                result.details.append(indent).append("  list=null (permission or kernel restriction)").append('\n');
                return;
            }
            Arrays.sort(children, new Comparator<File>() {
                @Override
                public int compare(File a, File b) {
                    return a.getName().compareTo(b.getName());
                }
            });
            for (File child : children) {
                if (result.walkedEntries >= maxEntries) {
                    result.details.append(indent).append("  ... entry limit reached ...\n");
                    return;
                }
                walk(child, depth + 1, maxDepth, maxEntries, result, visited);
            }
        }

        private int readGlobalInt(String name) {
            try {
                return Settings.Global.getInt(context.getContentResolver(), name, -1);
            } catch (Throwable t) {
                return -2;
            }
        }

        private void commandBlock(String title, String... command) {
            line("");
            line("### " + title);
            line("$ " + joinCommand(command));
            line(runCommand(title, command));
        }

        private String runCommand(String label, String... command) {
            Process process = null;
            try {
                ProcessBuilder builder = new ProcessBuilder(command);
                builder.redirectErrorStream(true);
                process = builder.start();
                String output = readStream(process.getInputStream(), MAX_COMMAND_CHARS);
                int exit = process.waitFor();
                return "exit=" + exit + "\n" + output;
            } catch (Throwable t) {
                return "ERROR: " + label + ": " + describe(t);
            } finally {
                if (process != null) {
                    try {
                        process.destroy();
                    } catch (Throwable ignored) {
                    }
                }
            }
        }

        private static String readStream(InputStream input, int maxChars) throws IOException {
            StringBuilder out = new StringBuilder();
            BufferedReader reader = new BufferedReader(new InputStreamReader(input, StandardCharsets.UTF_8));
            char[] buffer = new char[4096];
            int read;
            while ((read = reader.read(buffer)) >= 0) {
                int allowed = Math.min(read, maxChars - out.length());
                if (allowed > 0) {
                    out.append(buffer, 0, allowed);
                }
                if (out.length() >= maxChars) {
                    out.append("\n... output truncated ...");
                    break;
                }
            }
            return out.toString().trim();
        }

        private static String readSingleFile(String path, int maxBytes) {
            File file = new File(path);
            if (!file.exists()) {
                return "<absent>";
            }
            if (!file.canRead()) {
                return "<not-readable>";
            }
            try (FileInputStream input = new FileInputStream(file);
                 ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[1024];
                int total = 0;
                int read;
                while ((read = input.read(buffer, 0, Math.min(buffer.length, maxBytes - total))) > 0) {
                    output.write(buffer, 0, read);
                    total += read;
                    if (total >= maxBytes) {
                        break;
                    }
                }
                byte[] bytes = output.toByteArray();
                for (byte b : bytes) {
                    if (b == 0) {
                        return "<binary:" + bytes.length + " bytes>";
                    }
                }
                String text = new String(bytes, StandardCharsets.UTF_8).trim();
                if (file.length() > maxBytes) {
                    text += " ...<truncated>";
                }
                return text;
            } catch (Throwable t) {
                return "<read-error:" + describe(t) + ">";
            }
        }

        private String firstProperty(String... names) {
            for (String name : names) {
                String value = facts.properties.get(name);
                if (value != null && !value.trim().isEmpty()) {
                    return name + "=" + value;
                }
            }
            return "";
        }

        private static boolean isFalse(String value) {
            return "false".equalsIgnoreCase(value) || "0".equals(value);
        }

        private static String blankAsUnknown(String value) {
            return value == null || value.trim().isEmpty() ? "<unknown>" : value;
        }

        private static String settingValue(int value) {
            if (value == -2) {
                return "접근 오류";
            }
            if (value == -1) {
                return "값 없음";
            }
            return value + (value == 1 ? " (ON)" : value == 0 ? " (OFF)" : "");
        }

        private void section(String title) {
            report.append('\n')
                    .append("================================================================================\n")
                    .append(title).append('\n')
                    .append("================================================================================\n");
        }

        private void line(String text) {
            report.append(text == null ? "null" : text).append('\n');
        }

        private void kv(String key, String value) {
            line(key + ": " + (value == null ? "null" : value));
        }

        private void feature(String name, boolean present) {
            line(name + ": " + yesNo(present));
        }

        private static String yesNo(boolean value) {
            return value ? "YES" : "NO";
        }

        private static String hex(int value, int width) {
            return String.format(Locale.US, "%0" + width + "X", value);
        }

        private static String describe(Throwable t) {
            String message = t.getMessage();
            return t.getClass().getSimpleName() + (message == null ? "" : ": " + message);
        }

        private static String isoTime(Date date) {
            SimpleDateFormat format = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSXXX", Locale.US);
            format.setTimeZone(TimeZone.getDefault());
            return format.format(date);
        }

        private static String repeat(String value, int count) {
            StringBuilder out = new StringBuilder(value.length() * count);
            for (int i = 0; i < count; i++) {
                out.append(value);
            }
            return out.toString();
        }

        private static String joinCommand(String[] command) {
            StringBuilder out = new StringBuilder();
            for (int i = 0; i < command.length; i++) {
                if (i > 0) {
                    out.append(' ');
                }
                out.append(command[i]);
            }
            return out.toString();
        }

        private static String ipv4FromInt(int ip) {
            return (ip & 0xFF) + "."
                    + ((ip >> 8) & 0xFF) + "."
                    + ((ip >> 16) & 0xFF) + "."
                    + ((ip >> 24) & 0xFF);
        }

        private static final class ProbeResult {
            boolean exists;
            boolean directory;
            boolean canRead;
            int immediateEntries;
            int walkedEntries;
            String error;
            final StringBuilder details = new StringBuilder();
        }

        private static final class Facts {
            boolean usbHostFeature;
            boolean usbAccessoryFeature;
            boolean usbStateAvailable;
            int adbEnabledSetting = -1;
            int attachedUsbDeviceCount;
            final Map<String, String> usbState = new HashMap<>();
            final Map<String, String> properties = new HashMap<>();
            final Map<String, ProbeResult> probes = new HashMap<>();
        }
    }
}
