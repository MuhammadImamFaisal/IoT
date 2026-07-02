/*
  =====================================================================
  APLIKASI PENGUSIR HAMA - React Native (Expo)
  =====================================================================
  Fitur:
  - Setting alamat IP ESP32Dev (disimpan permanen di HP pakai AsyncStorage)
  - Lihat status mode (siang/malam) & ganti mode
  - Kontrol servo manual (kiri/tengah/kanan)
  - Riwayat 10 deteksi gerakan terakhir
  - Polling otomatis tiap 4 detik ke endpoint /status, munculin Alert
    popup + badge merah kalau ada deteksi gerakan baru sejak polling
    sebelumnya (dibandingkan dari field totalDeteksi)

  CATATAN PENTING:
  - HP kamu HARUS terhubung ke WiFi yang SAMA dengan ESP32Dev supaya
    app ini bisa connect (karena ESP32 cuma bisa diakses lewat jaringan
    lokal, bukan internet, kecuali nanti kamu setup lebih lanjut).
  - Notifikasi di sini sifatnya "polling": app harus dalam keadaan
    terbuka (foreground) supaya bisa mendeteksi gerakan baru. Kalau
    app ditutup/di-background lama, polling berhenti.
  =====================================================================
*/

import React, { useState, useEffect, useRef, useCallback } from 'react';
import {
  SafeAreaView,
  View,
  Text,
  TextInput,
  TouchableOpacity,
  StyleSheet,
  ScrollView,
  Alert,
  ActivityIndicator,
  RefreshControl,
  Animated,
} from 'react-native';
import AsyncStorage from '@react-native-async-storage/async-storage';

const STORAGE_KEY_IP = '@pengusir_hama_esp32_ip';
const TOAST_DURATION_MS = 3000; // toast otomatis hilang setelah 3 detik

// ====================================================================
// KOMPONEN: Toast notifikasi sederhana, muncul dari atas, auto-hilang
// sendiri, TIDAK ada tombol OK / tidak perlu di-tap.
// ====================================================================
function Toast({ message, visible }) {
  const translateY = useRef(new Animated.Value(-100)).current;

  useEffect(() => {
    if (visible) {
      Animated.sequence([
        Animated.spring(translateY, { toValue: 0, useNativeDriver: true, bounciness: 8 }),
        Animated.delay(TOAST_DURATION_MS),
        Animated.timing(translateY, { toValue: -100, duration: 300, useNativeDriver: true }),
      ]).start();
    }
  }, [visible, message]);

  if (!message) return null;

  return (
    <Animated.View style={[styles.toast, { transform: [{ translateY }] }]} pointerEvents="none">
      <Text style={styles.toastText}>{message}</Text>
    </Animated.View>
  );
}

export default function App() {
  const [esp32Ip, setEsp32Ip] = useState('');
  const [ipInput, setIpInput] = useState('');
  const [isEditingIp, setIsEditingIp] = useState(true);

  const [status, setStatus] = useState(null);     // hasil parse JSON dari /status
  const [loading, setLoading] = useState(false);
  const [refreshing, setRefreshing] = useState(false);
  const [connError, setConnError] = useState(null);
  const [lastUpdate, setLastUpdate] = useState(null);

  const lastTotalDeteksiRef = useRef(null); // dipakai buat bandingkan ada gerakan baru atau tidak
  const [toastMessage, setToastMessage] = useState('');
  const [toastVisible, setToastVisible] = useState(false);

  const showToast = (message) => {
    setToastMessage(message);
    setToastVisible(true);
    // reset trigger biar animasi ulang lagi kalau toast berikutnya muncul cepat
    setTimeout(() => setToastVisible(false), TOAST_DURATION_MS + 400);
  };

  // ------------------------------------------------------------------
  // Load IP yang sudah disimpan sebelumnya saat app pertama dibuka
  // ------------------------------------------------------------------
  useEffect(() => {
    (async () => {
      try {
        const savedIp = await AsyncStorage.getItem(STORAGE_KEY_IP);
        if (savedIp) {
          setEsp32Ip(savedIp);
          setIpInput(savedIp);
          setIsEditingIp(false);
        }
      } catch (e) {
        console.log('Gagal load IP tersimpan', e);
      }
    })();
  }, []);

  // ------------------------------------------------------------------
  // Simpan IP baru ke AsyncStorage
  // ------------------------------------------------------------------
  const simpanIp = async () => {
    const trimmed = ipInput.trim();
    if (!trimmed) {
      Alert.alert('IP Kosong', 'Masukkan alamat IP ESP32Dev terlebih dahulu.');
      return;
    }
    try {
      await AsyncStorage.setItem(STORAGE_KEY_IP, trimmed);
      setEsp32Ip(trimmed);
      setIsEditingIp(false);
      lastTotalDeteksiRef.current = null; // reset supaya gak langsung alert pas ganti IP
    } catch (e) {
      Alert.alert('Gagal menyimpan', 'Terjadi kesalahan saat menyimpan IP.');
    }
  };

  // ------------------------------------------------------------------
  // Ambil status dari ESP32 (/status)
  // ------------------------------------------------------------------
  const fetchStatus = useCallback(async (showAlertOnNewDetection = true) => {
    if (!esp32Ip) return;
    try {
      const res = await fetch(`http://${esp32Ip}/status`, { timeout: 5000 });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const data = await res.json();

      setStatus(data);
      setConnError(null);
      setLastUpdate(new Date());

      const totalBaru = data.totalDeteksi ?? 0;
      if (
        showAlertOnNewDetection &&
        lastTotalDeteksiRef.current !== null &&
        totalBaru > lastTotalDeteksiRef.current
      ) {
        const selisih = totalBaru - lastTotalDeteksiRef.current;
        showToast(`🐾 ${selisih} gerakan baru terdeteksi! Mode: ${data.mode?.toUpperCase()}`);
      }
      lastTotalDeteksiRef.current = totalBaru;
    } catch (e) {
      setConnError('Gagal terhubung ke ESP32. Cek IP & pastikan satu WiFi.');
    }
  }, [esp32Ip]);

  // ------------------------------------------------------------------
  // Cek status sekali saat app dibuka/IP disimpan, dan tiap kali user
  // refresh manual (pull-to-refresh / tombol). TIDAK ADA polling
  // otomatis berkelanjutan -- biar hemat baterai & request.
  // Popup muncul kalau totalDeteksi naik dibanding pengecekan terakhir.
  // ------------------------------------------------------------------
  useEffect(() => {
    if (!esp32Ip || isEditingIp) return;
    setLoading(true);
    fetchStatus(true).finally(() => setLoading(false));
  }, [esp32Ip, isEditingIp, fetchStatus]);

  const onRefresh = async () => {
    setRefreshing(true);
    await fetchStatus(true);
    setRefreshing(false);
  };

  // ------------------------------------------------------------------
  // Aksi: ganti mode siang/malam
  // ------------------------------------------------------------------
  const gantiMode = async (mode) => {
    try {
      await fetch(`http://${esp32Ip}/setmode?mode=${mode}`);
      fetchStatus(false);
    } catch (e) {
      Alert.alert('Gagal', 'Tidak bisa mengubah mode. Cek koneksi ke ESP32.');
    }
  };

  // ------------------------------------------------------------------
  // Aksi: gerakkan servo manual
  // ------------------------------------------------------------------
  const gerakServo = async (dir) => {
    try {
      await fetch(`http://${esp32Ip}/manual?dir=${dir}`);
    } catch (e) {
      Alert.alert('Gagal', 'Tidak bisa menggerakkan servo. Cek koneksi ke ESP32.');
    }
  };

  // ------------------------------------------------------------------
  // State & aksi untuk ESP32-CAM (IP terpisah dari ESP32Dev)
  // ------------------------------------------------------------------
  const [camIp, setCamIp] = useState('');
  const [camIpInput, setCamIpInput] = useState('');
  const [isEditingCamIp, setIsEditingCamIp] = useState(false);
  const [flashOn, setFlashOn] = useState(false);
  const [camLoading, setCamLoading] = useState(false);

  useEffect(() => {
    (async () => {
      try {
        const saved = await AsyncStorage.getItem('@pengusir_hama_cam_ip');
        if (saved) { setCamIp(saved); setCamIpInput(saved); }
      } catch (e) {}
    })();
  }, []);

  const simpanCamIp = async () => {
    const trimmed = camIpInput.trim();
    if (!trimmed) return;
    await AsyncStorage.setItem('@pengusir_hama_cam_ip', trimmed);
    setCamIp(trimmed);
    setIsEditingCamIp(false);
  };

  const capturePhoto = async () => {
    if (!camIp) { showToast('IP ESP32-CAM belum diset'); return; }
    setCamLoading(true);
    try {
      const modeStr = status?.mode === 'siang' ? 'SIANG' : 'MALAM';
      const res = await fetch(`http://${camIp}/send-photo?mode=${modeStr}`);
      if (res.ok) showToast('📸 Foto berhasil dikirim ke Telegram!');
      else showToast('❌ Gagal kirim foto ke Telegram');
    } catch (e) {
      showToast('❌ Gagal konek ke ESP32-CAM');
    }
    setCamLoading(false);
  };

  const toggleFlash = async () => {
    if (!camIp) { showToast('IP ESP32-CAM belum diset'); return; }
    try {
      const endpoint = flashOn ? '/flash/off' : '/flash/on';
      await fetch(`http://${camIp}${endpoint}`);
      setFlashOn(!flashOn);
      showToast(flashOn ? '🔦 Flash mati' : '🔦 Flash nyala');
    } catch (e) {
      showToast('❌ Gagal kontrol flash');
    }
  };

  // ====================================================================
  // RENDER: Halaman setting IP (kalau IP belum diset / lagi diedit)
  // ====================================================================
  if (isEditingIp) {
    return (
      <SafeAreaView style={styles.safeArea}>
        <View style={styles.centerContainer}>
          <Text style={styles.title}>🐀 Pengusir Hama</Text>
          <Text style={styles.subtitle}>Masukkan alamat IP ESP32Dev kamu</Text>
          <Text style={styles.hint}>
            (Lihat di Serial Monitor Arduino IDE setelah ESP32 connect WiFi, contoh: 192.168.1.20)
          </Text>
          <TextInput
            style={styles.input}
            placeholder="192.168.1.20"
            placeholderTextColor="#999"
            value={ipInput}
            onChangeText={setIpInput}
            keyboardType="numeric"
            autoCapitalize="none"
          />
          <TouchableOpacity style={styles.btnPrimary} onPress={simpanIp}>
            <Text style={styles.btnPrimaryText}>Simpan & Hubungkan</Text>
          </TouchableOpacity>
        </View>
      </SafeAreaView>
    );
  }

  // ====================================================================
  // RENDER: Dashboard utama
  // ====================================================================
  const modeSiang = status?.mode === 'siang';

  return (
    <SafeAreaView style={styles.safeArea}>
      <Toast message={toastMessage} visible={toastVisible} />
      <ScrollView
        contentContainerStyle={styles.scrollContent}
        refreshControl={<RefreshControl refreshing={refreshing} onRefresh={onRefresh} />}
      >
        <View style={styles.headerRow}>
          <Text style={styles.title}>🐀 Pengusir Hama</Text>
          <TouchableOpacity onPress={() => setIsEditingIp(true)}>
            <Text style={styles.linkText}>Ganti IP</Text>
          </TouchableOpacity>
        </View>

        <Text style={styles.ipText}>Terhubung ke: {esp32Ip}</Text>

        {connError && (
          <View style={styles.errorBox}>
            <Text style={styles.errorText}>⚠️ {connError}</Text>
          </View>
        )}

        {loading && !status ? (
          <ActivityIndicator size="large" color="#2c3e50" style={{ marginTop: 30 }} />
        ) : (
          <>
            {/* CARD: Mode */}
            <View style={styles.card}>
              <Text style={styles.cardTitle}>Mode Sistem</Text>
              <Text style={styles.modeText}>
                Aktif: <Text style={{ fontWeight: 'bold' }}>{status?.mode?.toUpperCase() ?? '-'}</Text>
              </Text>
              <View style={styles.row}>
                <TouchableOpacity
                  style={[styles.btnMode, modeSiang && styles.btnModeActive, { backgroundColor: '#f39c12' }]}
                  onPress={() => gantiMode('siang')}
                >
                  <Text style={styles.btnModeText}>☀️ Siang</Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[styles.btnMode, !modeSiang && styles.btnModeActive, { backgroundColor: '#2c3e50' }]}
                  onPress={() => gantiMode('malam')}
                >
                  <Text style={styles.btnModeText}>🌙 Malam</Text>
                </TouchableOpacity>
              </View>
            </View>

            {/* CARD: Kontrol Servo Manual */}
            <View style={styles.card}>
              <Text style={styles.cardTitle}>Kontrol Servo Manual</Text>
              <View style={styles.row}>
                <TouchableOpacity style={styles.btnServo} onPress={() => gerakServo('left')}>
                  <Text style={styles.btnServoText}>⬅️ Kiri</Text>
                </TouchableOpacity>
                <TouchableOpacity style={styles.btnServo} onPress={() => gerakServo('center')}>
                  <Text style={styles.btnServoText}>⏺️ Tengah</Text>
                </TouchableOpacity>
                <TouchableOpacity style={styles.btnServo} onPress={() => gerakServo('right')}>
                  <Text style={styles.btnServoText}>➡️ Kanan</Text>
                </TouchableOpacity>
              </View>
            </View>

            {/* CARD: ESP32-CAM */}
            <View style={styles.card}>
              <Text style={styles.cardTitle}>Kamera (ESP32-CAM)</Text>
              <View style={styles.row}>
                <Text style={[styles.statText, {flex:1}]}>
                  IP CAM: {camIp || 'Belum diset'}
                </Text>
                <TouchableOpacity onPress={() => setIsEditingCamIp(!isEditingCamIp)}>
                  <Text style={styles.linkText}>
                    {isEditingCamIp ? 'Batal' : 'Ganti'}
                  </Text>
                </TouchableOpacity>
              </View>
              {isEditingCamIp && (
                <View style={{marginTop: 8}}>
                  <TextInput
                    style={styles.input}
                    placeholder="192.168.x.x"
                    placeholderTextColor="#999"
                    value={camIpInput}
                    onChangeText={setCamIpInput}
                    keyboardType="numeric"
                    autoCapitalize="none"
                  />
                  <TouchableOpacity style={styles.btnPrimary} onPress={simpanCamIp}>
                    <Text style={styles.btnPrimaryText}>Simpan IP CAM</Text>
                  </TouchableOpacity>
                </View>
              )}
              <View style={[styles.row, {marginTop: 12}]}>
                <TouchableOpacity
                  style={[styles.btnServo, {backgroundColor: '#8e44ad', flex: 1.5}]}
                  onPress={capturePhoto}
                  disabled={camLoading}
                >
                  <Text style={styles.btnServoText}>
                    {camLoading ? 'Mengirim...' : '📸 Capture & Kirim Telegram'}
                  </Text>
                </TouchableOpacity>
                <TouchableOpacity
                  style={[styles.btnServo, {backgroundColor: flashOn ? '#f39c12' : '#555', flex: 1}]}
                  onPress={toggleFlash}
                >
                  <Text style={styles.btnServoText}>
                    {flashOn ? '🔦 Flash ON' : '🔦 Flash OFF'}
                  </Text>
                </TouchableOpacity>
              </View>
            </View>
            <View style={styles.card}>
              <Text style={styles.cardTitle}>Statistik</Text>
              <Text style={styles.statText}>Total deteksi sejak ESP32 nyala: {status?.totalDeteksi ?? 0}</Text>
              {lastUpdate && (
                <Text style={styles.statSubText}>
                  Update terakhir: {lastUpdate.toLocaleTimeString()}
                </Text>
              )}
            </View>

            {/* CARD: Riwayat */}
            <View style={styles.card}>
              <Text style={styles.cardTitle}>Riwayat Deteksi (terbaru di atas)</Text>
              {(!status?.riwayat || status.riwayat.length === 0) ? (
                <Text style={styles.emptyText}>Belum ada gerakan terdeteksi.</Text>
              ) : (
                status.riwayat.map((item, idx) => (
                  <Text key={idx} style={styles.riwayatItem}>• {item}</Text>
                ))
              )}
            </View>
          </>
        )}

        <Text style={styles.footerNote}>
          Pastikan HP & ESP32Dev terhubung di WiFi yang sama. Tarik ke bawah (pull-to-refresh) untuk memeriksa status & gerakan terbaru.
        </Text>
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safeArea: { flex: 1, backgroundColor: '#f2f2f2' },
  scrollContent: { padding: 20, paddingBottom: 40 },
  centerContainer: { flex: 1, justifyContent: 'center', padding: 24 },

  title: { fontSize: 24, fontWeight: 'bold', color: '#2c3e50' },
  subtitle: { fontSize: 16, color: '#555', marginTop: 12, textAlign: 'center' },
  hint: { fontSize: 12, color: '#999', marginTop: 6, marginBottom: 20, textAlign: 'center' },

  input: {
    borderWidth: 1,
    borderColor: '#ccc',
    borderRadius: 10,
    padding: 14,
    fontSize: 16,
    backgroundColor: '#fff',
    marginBottom: 16,
  },
  btnPrimary: {
    backgroundColor: '#27ae60',
    padding: 16,
    borderRadius: 10,
    alignItems: 'center',
  },
  btnPrimaryText: { color: '#fff', fontWeight: 'bold', fontSize: 16 },

  headerRow: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', marginBottom: 4 },
  linkText: { color: '#3498db', fontSize: 14 },
  ipText: { color: '#777', fontSize: 13, marginBottom: 16 },

  errorBox: { backgroundColor: '#ffe5e5', padding: 12, borderRadius: 8, marginBottom: 16 },
  errorText: { color: '#c0392b', fontSize: 13 },

  card: {
    backgroundColor: '#fff',
    borderRadius: 12,
    padding: 18,
    marginBottom: 16,
    shadowColor: '#000',
    shadowOpacity: 0.06,
    shadowRadius: 6,
    elevation: 2,
  },
  cardTitle: { fontSize: 16, fontWeight: 'bold', color: '#2c3e50', marginBottom: 10 },
  modeText: { fontSize: 15, color: '#444', marginBottom: 12 },

  row: { flexDirection: 'row', justifyContent: 'space-between', gap: 8 },

  btnMode: { flex: 1, padding: 12, borderRadius: 8, alignItems: 'center', opacity: 0.55 },
  btnModeActive: { opacity: 1 },
  btnModeText: { color: '#fff', fontWeight: 'bold' },

  btnServo: { flex: 1, backgroundColor: '#3498db', padding: 12, borderRadius: 8, alignItems: 'center', marginHorizontal: 2 },
  btnServoText: { color: '#fff', fontWeight: 'bold', fontSize: 13 },

  statText: { fontSize: 15, color: '#444' },
  statSubText: { fontSize: 12, color: '#999', marginTop: 4 },

  emptyText: { color: '#999', fontStyle: 'italic' },
  riwayatItem: { fontSize: 13, color: '#444', marginBottom: 4 },

  footerNote: { fontSize: 11, color: '#aaa', textAlign: 'center', marginTop: 10 },

  toast: {
    position: 'absolute',
    top: 0,
    left: 16,
    right: 16,
    backgroundColor: '#2c3e50',
    borderRadius: 12,
    paddingVertical: 14,
    paddingHorizontal: 18,
    marginTop: 12,
    zIndex: 999,
    shadowColor: '#000',
    shadowOpacity: 0.25,
    shadowRadius: 8,
    shadowOffset: { width: 0, height: 4 },
    elevation: 6,
  },
  toastText: { color: '#fff', fontSize: 14, fontWeight: '600', textAlign: 'center' },
});