/**
 * Zygisk Device Spoofer - WebUI Logic
 * Universal Identity Manager with 30+ real device profiles
 */

// ============================================================================
// Shell Execution (KernelSU / APatch / Magisk)
// ============================================================================
async function execShell(command) {
    try {
        if (typeof ksu !== 'undefined' && ksu.exec) return await ksu.exec(command);
        if (typeof apatch !== 'undefined' && apatch.exec) return await apatch.exec(command);
        if (typeof magisk !== 'undefined' && magisk.exec) return await magisk.exec(command);
        console.warn('No root manager API found');
        return { errno: -1, stdout: '', stderr: 'No root manager API' };
    } catch (e) {
        return { errno: -1, stdout: '', stderr: e.message };
    }
}

const CONFIG_PATH = '/data/adb/modules/zygisk_spoofer/config.json';
const WIPE_SCRIPT = '/data/adb/modules/zygisk_spoofer/wipe.sh';

// ============================================================================
// Device Profiles Database (30+ real devices)
// ============================================================================
const DEVICE_PROFILES = [
    // === SAMSUNG ===
    { model:'SM-S928B', brand:'samsung', manufacturer:'Samsung', device:'dm3q', product:'dm3qxx', board:'s5e9945', hardware:'exynos2400',
      fp:'samsung/dm3qxx/dm3q:14/UP1A.231005.007/S928BXXS2AXA1:user/release-keys', bootloader:'S928BXXS2AXA1',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007.S928BXXS2AXA1', gl_renderer:'Mali-G720 Immortalis MC12', gl_vendor:'ARM',
      density:'640', resolution:'1440x3120', soc:'s5e9945', baseband:'S928BXXS2AXA1' },
    { model:'SM-S926B', brand:'samsung', manufacturer:'Samsung', device:'e2q', product:'e2qxx', board:'s5e9945', hardware:'exynos2400',
      fp:'samsung/e2qxx/e2q:14/UP1A.231005.007/S926BXXS2AXB2:user/release-keys', bootloader:'S926BXXS2AXB2',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007.S926BXXS2AXB2', gl_renderer:'Mali-G720 Immortalis MC12', gl_vendor:'ARM',
      density:'480', resolution:'1080x2340', soc:'s5e9945', baseband:'S926BXXS2AXB2' },
    { model:'SM-A546E', brand:'samsung', manufacturer:'Samsung', device:'a54x', product:'a54xnsxx', board:'s5e8835', hardware:'exynos1380',
      fp:'samsung/a54xnsxx/a54x:14/UP1A.231005.007/A546EXXU7CXB3:user/release-keys', bootloader:'A546EXXU7CXB3',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007.A546EXXU7CXB3', gl_renderer:'Mali-G68 MC4', gl_vendor:'ARM',
      density:'393', resolution:'1080x2340', soc:'s5e8835', baseband:'A546EXXU7CXB3' },
    { model:'SM-A156E', brand:'samsung', manufacturer:'Samsung', device:'a15', product:'a15nsxx', board:'s5e8535', hardware:'exynos850',
      fp:'samsung/a15nsxx/a15:14/UP1A.231005.007/A156EXXU2AXA1:user/release-keys', bootloader:'A156EXXU2AXA1',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007.A156EXXU2AXA1', gl_renderer:'Mali-G52 MC1', gl_vendor:'ARM',
      density:'450', resolution:'1080x2340', soc:'s5e8535', baseband:'A156EXXU2AXA1' },
    { model:'SM-G998B', brand:'samsung', manufacturer:'Samsung', device:'o1s', product:'o1sxeea', board:'exynos2100', hardware:'exynos2100',
      fp:'samsung/o1sxeea/o1s:14/UP1A.231005.007/G998BXXS9FXA1:user/release-keys', bootloader:'G998BXXS9FXA1',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007.G998BXXS9FXA1', gl_renderer:'Mali-G78 MP14', gl_vendor:'ARM',
      density:'640', resolution:'1440x3200', soc:'exynos2100', baseband:'G998BXXS9FXA1' },
    { model:'SM-F946B', brand:'samsung', manufacturer:'Samsung', device:'q5q', product:'q5qxx', board:'s5e9945', hardware:'exynos2400',
      fp:'samsung/q5qxx/q5q:14/UP1A.231005.007/F946BXXU1AXC1:user/release-keys', bootloader:'F946BXXU1AXC1',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007.F946BXXU1AXC1', gl_renderer:'Mali-G720 Immortalis MC12', gl_vendor:'ARM',
      density:'420', resolution:'1856x2160', soc:'s5e9945', baseband:'F946BXXU1AXC1' },

    // === GOOGLE PIXEL ===
    { model:'Pixel 9 Pro', brand:'google', manufacturer:'Google', device:'caiman', product:'caiman', board:'ripcurrent', hardware:'tensor',
      fp:'google/caiman/caiman:15/AP4A.250205.004/12716org:user/release-keys', bootloader:'slider-15.0-12716org',
      build_id:'AP4A.250205.004', display:'AP4A.250205.004', gl_renderer:'Mali-G715 Immortalis MC10', gl_vendor:'ARM',
      density:'560', resolution:'1280x2856', soc:'Tensor G4', baseband:'Modem_GS_24.2.382443' },
    { model:'Pixel 8 Pro', brand:'google', manufacturer:'Google', device:'husky', product:'husky', board:'ripcurrent', hardware:'tensor',
      fp:'google/husky/husky:14/UD1A.231105.004/11010374:user/release-keys', bootloader:'slider-14.0-11010374',
      build_id:'UD1A.231105.004', display:'UD1A.231105.004', gl_renderer:'Mali-G715 Immortalis MC10', gl_vendor:'ARM',
      density:'560', resolution:'1344x2992', soc:'Tensor G3', baseband:'Modem_GS_23.3.230904' },
    { model:'Pixel 8', brand:'google', manufacturer:'Google', device:'shiba', product:'shiba', board:'ripcurrent', hardware:'tensor',
      fp:'google/shiba/shiba:14/UD1A.231105.004/11010374:user/release-keys', bootloader:'slider-14.0-11010374',
      build_id:'UD1A.231105.004', display:'UD1A.231105.004', gl_renderer:'Mali-G715 Immortalis MC10', gl_vendor:'ARM',
      density:'420', resolution:'1080x2400', soc:'Tensor G3', baseband:'Modem_GS_23.3.230904' },
    { model:'Pixel 7a', brand:'google', manufacturer:'Google', device:'lynx', product:'lynx', board:'lynx', hardware:'tensor',
      fp:'google/lynx/lynx:14/UQ1A.240205.002/11224170:user/release-keys', bootloader:'slider-14.0-11224170',
      build_id:'UQ1A.240205.002', display:'UQ1A.240205.002', gl_renderer:'Mali-G710 MC10', gl_vendor:'ARM',
      density:'420', resolution:'1080x2400', soc:'Tensor G2', baseband:'Modem_GS_22.1.221130' },

    // === XIAOMI ===
    { model:'23127PN0CG', brand:'Xiaomi', manufacturer:'Xiaomi', device:'shennong', product:'shennong', board:'shennong', hardware:'qcom',
      fp:'Xiaomi/shennong/shennong:14/UKQ1.231003.002/V816.0.4.0.UNACNXM:user/release-keys', bootloader:'unknown',
      build_id:'UKQ1.231003.002', display:'UKQ1.231003.002.V816.0.4.0.UNACNXM', gl_renderer:'Adreno (TM) 750', gl_vendor:'Qualcomm',
      density:'440', resolution:'1440x3200', soc:'SM8650', baseband:'1.0.c4-00291' },
    { model:'22101316G', brand:'Xiaomi', manufacturer:'Xiaomi', device:'marble', product:'marble_global', board:'marble', hardware:'qcom',
      fp:'Xiaomi/marble_global/marble:14/UKQ1.230804.001/V816.0.6.0.UMRMIXM:user/release-keys', bootloader:'unknown',
      build_id:'UKQ1.230804.001', display:'UKQ1.230804.001.V816.0.6.0.UMRMIXM', gl_renderer:'Adreno (TM) 730', gl_vendor:'Qualcomm',
      density:'440', resolution:'1080x2400', soc:'SM8475', baseband:'1.0.c3-00181' },
    { model:'2201116SG', brand:'Xiaomi', manufacturer:'Xiaomi', device:'thor', product:'thor_global', board:'thor', hardware:'qcom',
      fp:'Xiaomi/thor_global/thor:14/UKQ1.230804.001/V816.0.3.0.ULLMIXM:user/release-keys', bootloader:'unknown',
      build_id:'UKQ1.230804.001', display:'UKQ1.230804.001.V816.0.3.0.ULLMIXM', gl_renderer:'Adreno (TM) 730', gl_vendor:'Qualcomm',
      density:'560', resolution:'1440x3200', soc:'SM8475', baseband:'1.0.c3-00181' },
    { model:'23078RKD5C', brand:'Redmi', manufacturer:'Xiaomi', device:'fire', product:'fire_global', board:'fire', hardware:'mt6833',
      fp:'Redmi/fire_global/fire:14/UP1A.230905.011/V816.0.5.0.UNZMIXM:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.230905.011', display:'UP1A.230905.011.V816.0.5.0.UNZMIXM', gl_renderer:'Mali-G57 MC2', gl_vendor:'ARM',
      density:'393', resolution:'1080x2400', soc:'MT6833', baseband:'MOLY.NR17.R1.TC8.SP' },
    { model:'23028RNCAG', brand:'POCO', manufacturer:'Xiaomi', device:'moonstone', product:'moonstone_p_global', board:'moonstone', hardware:'qcom',
      fp:'POCO/moonstone_p_global/moonstone:14/UKQ1.230804.001/V816.0.4.0.UMPMIXM:user/release-keys', bootloader:'unknown',
      build_id:'UKQ1.230804.001', display:'UKQ1.230804.001.V816.0.4.0.UMPMIXM', gl_renderer:'Adreno (TM) 619', gl_vendor:'Qualcomm',
      density:'393', resolution:'1080x2400', soc:'SM6375', baseband:'MPSS.HI.3.0' },

    // === OPPO ===
    { model:'CPH2581', brand:'OPPO', manufacturer:'OPPO', device:'OP5AFFL1', product:'CPH2581', board:'taro', hardware:'qcom',
      fp:'OPPO/CPH2581/OP5AFFL1:14/UP1A.231005.007/S.11836f7-1:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.231005.007', display:'CPH2581_14.0.0.600', gl_renderer:'Adreno (TM) 730', gl_vendor:'Qualcomm',
      density:'480', resolution:'1240x2772', soc:'SM8475', baseband:'MPSS.DE.1.0' },
    { model:'CPH2557', brand:'OPPO', manufacturer:'OPPO', device:'OP5D4FL1', product:'CPH2557', board:'lahaina', hardware:'qcom',
      fp:'OPPO/CPH2557/OP5D4FL1:14/UP1A.231005.007/S.192f7a2-1:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.231005.007', display:'CPH2557_14.0.0.503', gl_renderer:'Adreno (TM) 660', gl_vendor:'Qualcomm',
      density:'480', resolution:'1080x2400', soc:'SM8350', baseband:'MPSS.DE.1.0' },

    // === VIVO ===
    { model:'V2254A', brand:'vivo', manufacturer:'vivo', device:'V2254A', product:'V2254A', board:'taro', hardware:'qcom',
      fp:'vivo/V2254A/V2254A:14/UP1A.231005.007/compiler0827232458:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007 compiler0827232458', gl_renderer:'Adreno (TM) 730', gl_vendor:'Qualcomm',
      density:'480', resolution:'1260x2800', soc:'SM8475', baseband:'MPSS.DE.1.0' },
    { model:'V2324', brand:'vivo', manufacturer:'vivo', device:'V2324', product:'V2324', board:'kalama', hardware:'qcom',
      fp:'vivo/V2324/V2324:14/UP1A.231005.007/compiler1106165823:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.231005.007', display:'UP1A.231005.007 compiler1106165823', gl_renderer:'Adreno (TM) 740', gl_vendor:'Qualcomm',
      density:'480', resolution:'1260x2800', soc:'SM8550', baseband:'MPSS.DE.1.0' },

    // === REALME ===
    { model:'RMX3630', brand:'realme', manufacturer:'realme', device:'RMX3630', product:'RMX3630', board:'mt6895', hardware:'mt6895',
      fp:'realme/RMX3630/RMX3630:14/UP1A.231005.007/R.18ee708_1:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.231005.007', display:'RMX3630_14.0.0.800', gl_renderer:'Mali-G610 MC6', gl_vendor:'ARM',
      density:'480', resolution:'1240x2772', soc:'MT6895', baseband:'MOLY.NR17.R1' },
    { model:'RMX3890', brand:'realme', manufacturer:'realme', device:'RMX3890', product:'RMX3890', board:'kalama', hardware:'qcom',
      fp:'realme/RMX3890/RMX3890:14/UP1A.231005.007/R.1c33a8f-1:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.231005.007', display:'RMX3890_14.0.0.700', gl_renderer:'Adreno (TM) 740', gl_vendor:'Qualcomm',
      density:'480', resolution:'1240x2772', soc:'SM8550', baseband:'MPSS.DE.1.0' },

    // === ONEPLUS ===
    { model:'CPH2649', brand:'OnePlus', manufacturer:'OnePlus', device:'CPH2649', product:'CPH2649', board:'kalama', hardware:'qcom',
      fp:'OnePlus/CPH2649/OP5D58L1:14/UKQ1.230924.001/U.R4T3.172fcdd-13:user/release-keys', bootloader:'unknown',
      build_id:'UKQ1.230924.001', display:'CPH2649_14.0.0.406', gl_renderer:'Adreno (TM) 740', gl_vendor:'Qualcomm',
      density:'480', resolution:'1240x2772', soc:'SM8550', baseband:'MPSS.DE.1.0' },
    { model:'IN2025', brand:'OnePlus', manufacturer:'OnePlus', device:'OnePlus8Pro', product:'OnePlus8Pro', board:'kona', hardware:'qcom',
      fp:'OnePlus/OnePlus8Pro/OnePlus8Pro:14/UKQ1.230804.001/R.16daac4-1:user/release-keys', bootloader:'unknown',
      build_id:'UKQ1.230804.001', display:'IN2025_14.0.0.510', gl_renderer:'Adreno (TM) 650', gl_vendor:'Qualcomm',
      density:'480', resolution:'1440x3168', soc:'SM8250', baseband:'MPSS.HE.2.0' },

    // === HONOR ===
    { model:'ANY-LX1', brand:'HONOR', manufacturer:'HONOR', device:'HNANY-Q', product:'ANY-LX1', board:'HNANY', hardware:'kirin',
      fp:'HONOR/ANY-LX1/HNANY-Q:14/HONORANY-N00/104.0.0.66:user/release-keys', bootloader:'unknown',
      build_id:'HONORANY-N00', display:'ANY-LX1 14.0.0.66', gl_renderer:'Mali-G78 MP24', gl_vendor:'ARM',
      density:'480', resolution:'1200x2664', soc:'Kirin 9000S', baseband:'21C80B516S000C000' },

    // === MOTOROLA ===
    { model:'motorola edge 40 pro', brand:'motorola', manufacturer:'Motorola', device:'rtwo', product:'rtwo_g', board:'kalama', hardware:'qcom',
      fp:'motorola/rtwo_g/rtwo:14/U1TRS34.39-18/34c9b:user/release-keys', bootloader:'unknown',
      build_id:'U1TRS34.39-18', display:'U1TRS34.39-18-34c9b', gl_renderer:'Adreno (TM) 740', gl_vendor:'Qualcomm',
      density:'400', resolution:'1080x2400', soc:'SM8550', baseband:'M8350_MPSS.DE' },
    { model:'moto g84 5G', brand:'motorola', manufacturer:'Motorola', device:'bangkk', product:'bangkk_g', board:'sm6375', hardware:'qcom',
      fp:'motorola/bangkk_g/bangkk:14/U1TBS34.41-28-2/52fb5:user/release-keys', bootloader:'unknown',
      build_id:'U1TBS34.41-28-2', display:'U1TBS34.41-28-2-52fb5', gl_renderer:'Adreno (TM) 619', gl_vendor:'Qualcomm',
      density:'400', resolution:'1080x2400', soc:'SM6375', baseband:'MPSS.HI.3.0' },

    // === NOTHING ===
    { model:'A063', brand:'Nothing', manufacturer:'Nothing', device:'Pong', product:'Pong', board:'kalama', hardware:'qcom',
      fp:'Nothing/Pong/Pong:14/UP1A.231005.007/2401151833:user/release-keys', bootloader:'unknown',
      build_id:'UP1A.231005.007', display:'Pong-U2.6-241115-1833', gl_renderer:'Adreno (TM) 740', gl_vendor:'Qualcomm',
      density:'420', resolution:'1080x2412', soc:'SM8550', baseband:'MPSS.DE.1.0' },

    // === ASUS ===
    { model:'ASUS_AI2401', brand:'asus', manufacturer:'Asus', device:'AI2401', product:'WW_AI2401', board:'kalama', hardware:'qcom',
      fp:'asus/WW_AI2401/AI2401:14/UKQ1.231003.002/38.1030.0801.72:user/release-keys', bootloader:'unknown',
      build_id:'UKQ1.231003.002', display:'WW_38.1030.0801.72', gl_renderer:'Adreno (TM) 740', gl_vendor:'Qualcomm',
      density:'440', resolution:'1080x2400', soc:'SM8550', baseband:'MPSS.DE.1.0' },

    // === SONY ===
    { model:'XQ-DQ72', brand:'Sony', manufacturer:'Sony', device:'pdx234', product:'pdx234', board:'kalama', hardware:'qcom',
      fp:'Sony/pdx234/pdx234:14/67.2.A.2.45/067002A002004500:user/release-keys', bootloader:'unknown',
      build_id:'67.2.A.2.45', display:'67.2.A.2.45', gl_renderer:'Adreno (TM) 740', gl_vendor:'Qualcomm',
      density:'420', resolution:'1080x2520', soc:'SM8550', baseband:'MPSS.DE.1.0' },
];

// All spoof fields
const SPOOF_FIELDS = [
    'android_id','gsf_id','imei','meid','serial','bootloader',
    'mac_address','bluetooth_mac','wifi_ssid','wifi_bssid',
    'model','brand','manufacturer','device','product','board','hardware',
    'build_id','fingerprint','display','build_type','build_tags','build_user','build_host','incremental',
    'baseband','operator_name','operator_numeric',
    'gl_renderer','gl_vendor','screen_density','screen_resolution','cpu_abi','soc_model','hardware_serial'
];

// Indonesian operators
const OPERATORS = [
    { name:'Telkomsel', numeric:'51010' },
    { name:'Indosat Ooredoo', numeric:'51001' },
    { name:'XL Axiata', numeric:'51011' },
    { name:'3 (Three)', numeric:'51089' },
    { name:'Smartfren', numeric:'51028' },
    { name:'by.U', numeric:'51010' },
];

// ============================================================================
// State
// ============================================================================
let config = { target_apps: [], spoof_values: {}, custom_wipe_dirs: [], enabled: true };
let installedApps = [];
let selectedApp = '';
let dropdownOpen = false;

// ============================================================================
// Randomizers
// ============================================================================
function randomHex(len) {
    const c = '0123456789abcdef';
    let r = ''; for (let i = 0; i < len; i++) r += c[Math.floor(Math.random() * 16)];
    return r;
}
function randomMAC() {
    const b = [(Math.floor(Math.random() * 256) | 0x02) & 0xFE];
    for (let i = 0; i < 5; i++) b.push(Math.floor(Math.random() * 256));
    return b.map(x => x.toString(16).padStart(2, '0')).join(':');
}
function randomSerial() {
    const c = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ';
    let r = ''; for (let i = 0; i < 11; i++) r += c[Math.floor(Math.random() * c.length)];
    return r;
}
function randomIMEI() {
    const tac = ['35','86','01','45','91','49','01','86'][Math.floor(Math.random() * 8)];
    let imei = tac;
    for (let i = 0; i < 12; i++) imei += Math.floor(Math.random() * 10).toString();
    let sum = 0;
    for (let i = 0; i < 14; i++) {
        let d = parseInt(imei[i]); if (i % 2 !== 0) { d *= 2; if (d > 9) d -= 9; } sum += d;
    }
    return imei + ((10 - (sum % 10)) % 10).toString();
}
function randomMEID() { return randomHex(14).toUpperCase(); }
function randomSSID() {
    const n = ['HOME-','TP-Link_','ASUS_','Linksys_','NETGEAR_','WiFi-','HUAWEI-','ZTE-','PLDT_','Indihome_'];
    return n[Math.floor(Math.random() * n.length)] + randomHex(4).toUpperCase();
}

// ============================================================================
// Core
// ============================================================================
async function loadConfig() {
    const r = await execShell(`cat ${CONFIG_PATH} 2>/dev/null`);
    if (r.errno === 0 && r.stdout) { try { config = JSON.parse(r.stdout); } catch(e) {} }
    renderUI(); updateStatus();
}

function renderUI() {
    const sv = config.spoof_values || {};
    SPOOF_FIELDS.forEach(id => { const el = document.getElementById(id); if (el) el.value = sv[id] || ''; });
    renderAppList(); renderWipeDirList();
}

function renderAppList() {
    const c = document.getElementById('appList'); c.innerHTML = '';
    (config.target_apps || []).forEach((app, i) => {
        const d = document.createElement('div'); d.className = 'app-item';
        d.innerHTML = `<span>${app}</span><button class="remove-btn" onclick="removeTargetApp(${i})"><i data-lucide="x" style="width:14px;height:14px"></i></button>`;
        c.appendChild(d);
    });
    if (typeof lucide !== 'undefined') lucide.createIcons();
}

function renderWipeDirList() {
    const c = document.getElementById('wipeDirList'); c.innerHTML = '';
    (config.custom_wipe_dirs || []).forEach((dir, i) => {
        const d = document.createElement('div'); d.className = 'app-item';
        d.innerHTML = `<span>/sdcard/${dir}</span><button class="remove-btn" onclick="removeWipeDir(${i})"><i data-lucide="x" style="width:14px;height:14px"></i></button>`;
        c.appendChild(d);
    });
    if (typeof lucide !== 'undefined') lucide.createIcons();
}

function updateStatus() {
    const badge = document.getElementById('statusBadge');
    const text = document.getElementById('statusText');
    const n = (config.target_apps || []).length;
    if (config.enabled && n > 0) { badge.classList.add('active'); text.textContent = `Active \u00b7 ${n} apps`; }
    else { badge.classList.remove('active'); text.textContent = config.enabled ? 'No targets' : 'Disabled'; }
}

// ============================================================================
// App / Dir Management
// ============================================================================
async function fetchInstalledApps() {
    const loading = document.getElementById('dropdownLoading');
    const itemsContainer = document.getElementById('dropdownItems');
    loading.style.display = 'flex';
    itemsContainer.innerHTML = '';
    try {
        const r = await execShell('pm list packages -3 2>/dev/null || pm list packages 2>/dev/null');
        if (r.errno === 0 && r.stdout) {
            installedApps = r.stdout.split('\n')
                .map(l => l.replace('package:', '').trim())
                .filter(p => p && p.includes('.'))
                .sort();
        }
    } catch(e) { console.error('Failed to fetch apps', e); }
    loading.style.display = 'none';
    renderDropdownItems();
}

function renderDropdownItems(filter = '') {
    const itemsContainer = document.getElementById('dropdownItems');
    const emptyEl = document.getElementById('dropdownEmpty');
    itemsContainer.innerHTML = '';
    const q = filter.toLowerCase();
    const filtered = q ? installedApps.filter(a => a.toLowerCase().includes(q)) : installedApps;
    const already = config.target_apps || [];
    const available = filtered.filter(a => !already.includes(a));
    if (available.length === 0) {
        emptyEl.style.display = 'block';
    } else {
        emptyEl.style.display = 'none';
        available.forEach(pkg => {
            const item = document.createElement('div');
            item.className = 'dropdown-item' + (pkg === selectedApp ? ' selected' : '');
            item.textContent = pkg;
            item.onclick = (e) => { e.stopPropagation(); selectApp(pkg); };
            itemsContainer.appendChild(item);
        });
    }
}

function selectApp(pkg) {
    selectedApp = pkg;
    document.getElementById('appSearchInput').value = pkg;
    closeDropdown();
}

function toggleDropdown() {
    if (dropdownOpen) closeDropdown(); else openDropdown();
}

function openDropdown() {
    if (dropdownOpen) return;
    dropdownOpen = true;
    document.getElementById('dropdownMenu').classList.add('open');
    document.getElementById('dropdownChevron').style.transform = 'rotate(180deg)';
    if (installedApps.length === 0) fetchInstalledApps();
    else renderDropdownItems(document.getElementById('appSearchInput').value);
}

function closeDropdown() {
    dropdownOpen = false;
    document.getElementById('dropdownMenu').classList.remove('open');
    document.getElementById('dropdownChevron').style.transform = '';
}

function filterApps() {
    const q = document.getElementById('appSearchInput').value;
    selectedApp = '';
    if (!dropdownOpen) openDropdown();
    renderDropdownItems(q);
}

function addSelectedApp() {
    const input = document.getElementById('appSearchInput');
    const pkg = (selectedApp || input.value.trim());
    if (!pkg || !pkg.includes('.')) { showToast('Select an app first','error'); return; }
    if (config.target_apps.includes(pkg)) { showToast('Already added','error'); return; }
    config.target_apps.push(pkg);
    selectedApp = ''; input.value = '';
    renderAppList(); updateStatus(); renderDropdownItems();
    showToast(`Added ${pkg}`,'success');
}

function removeTargetApp(i) {
    const r = config.target_apps.splice(i,1)[0];
    renderAppList(); updateStatus(); showToast(`Removed ${r}`,'success');
}
function addWipeDir() {
    const input = document.getElementById('newWipeDirInput');
    const dir = input.value.trim().replace(/^\/+/,'');
    if (!dir) return;
    if (!config.custom_wipe_dirs) config.custom_wipe_dirs = [];
    if (config.custom_wipe_dirs.includes(dir)) { showToast('Already added','error'); return; }
    config.custom_wipe_dirs.push(dir); input.value = '';
    renderWipeDirList(); showToast(`Added /sdcard/${dir}`,'success');
}
function removeWipeDir(i) {
    config.custom_wipe_dirs.splice(i,1);
    renderWipeDirList(); showToast('Removed','success');
}

function collectFormValues() {
    const sv = {};
    SPOOF_FIELDS.forEach(id => { const el = document.getElementById(id); if (el) sv[id] = el.value.trim(); });
    config.spoof_values = sv;
}

// ============================================================================
// Randomize
// ============================================================================
function randomizeAll() {
    const p = DEVICE_PROFILES[Math.floor(Math.random() * DEVICE_PROFILES.length)];
    const op = OPERATORS[Math.floor(Math.random() * OPERATORS.length)];
    const vals = {
        android_id: randomHex(16), gsf_id: randomHex(16),
        imei: randomIMEI(), meid: randomMEID(),
        serial: randomSerial(), bootloader: p.bootloader,
        mac_address: randomMAC(), bluetooth_mac: randomMAC(),
        wifi_ssid: randomSSID(), wifi_bssid: randomMAC(),
        model: p.model, brand: p.brand, manufacturer: p.manufacturer,
        device: p.device, product: p.product, board: p.board, hardware: p.hardware,
        build_id: p.build_id, fingerprint: p.fp, display: p.display,
        build_type: 'user', build_tags: 'release-keys',
        build_user: 'android-build', build_host: 'build.android.com',
        incremental: `V${randomHex(4).toUpperCase()}`,
        baseband: p.baseband, operator_name: op.name, operator_numeric: op.numeric,
        gl_renderer: p.gl_renderer, gl_vendor: p.gl_vendor,
        screen_density: p.density, screen_resolution: p.resolution,
        cpu_abi: 'arm64-v8a', soc_model: p.soc,
        hardware_serial: randomHex(8).toUpperCase(),
    };
    SPOOF_FIELDS.forEach(id => { const el = document.getElementById(id); if (el && vals[id] !== undefined) el.value = vals[id]; });
    collectFormValues();
    showToast(`${p.brand} ${p.model}`, 'success');
}

// ============================================================================
// Save / Wipe / Burn
// ============================================================================
async function saveConfig() {
    const btn = document.getElementById('btnSave'); btn.classList.add('loading');
    collectFormValues(); config.enabled = true;
    const json = JSON.stringify(config, null, 2);
    const escaped = json.replace(/'/g, "'\\''");
    const r = await execShell(`echo '${escaped}' > ${CONFIG_PATH}`);
    btn.classList.remove('loading');
    showToast(r.errno === 0 ? 'Config saved! Restart target apps.' : 'Save failed', r.errno === 0 ? 'success' : 'error');
}

async function wipeAllTargets() {
    if (!config.target_apps?.length) { showToast('No targets','error'); return; }
    const btn = document.getElementById('btnWipeAll'); btn.classList.add('loading');
    const r = await execShell(`sh ${WIPE_SCRIPT} ${config.target_apps.join(' ')}`);
    btn.classList.remove('loading');
    showToast(r.errno === 0 ? `Wiped ${config.target_apps.length} apps` : 'Wipe failed', r.errno === 0 ? 'success' : 'error');
}

async function burnAndSpoof() {
    const btn = document.getElementById('btnBurnSpoof'); btn.classList.add('loading');
    randomizeAll(); collectFormValues(); config.enabled = true;
    const json = JSON.stringify(config, null, 2);
    const escaped = json.replace(/'/g, "'\\''");
    await execShell(`echo '${escaped}' > ${CONFIG_PATH}`);
    if (config.target_apps?.length) await execShell(`sh ${WIPE_SCRIPT} ${config.target_apps.join(' ')}`);
    btn.classList.remove('loading');
    showToast('Identity burned! Open target apps.', 'success');
}

// ============================================================================
// Toast
// ============================================================================
let toastTimeout = null;
function showToast(msg, type = 'success') {
    const toast = document.getElementById('toast');
    const textEl = document.getElementById('toastText');
    textEl.textContent = msg;
    toast.className = `toast show ${type}`;
    if (toastTimeout) clearTimeout(toastTimeout);
    toastTimeout = setTimeout(() => toast.className = 'toast', 3000);
}

// ============================================================================
// Init
// ============================================================================
document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('newWipeDirInput').addEventListener('keydown', e => { if (e.key === 'Enter') addWipeDir(); });
    document.getElementById('appSearchInput').addEventListener('keydown', e => { if (e.key === 'Enter') addSelectedApp(); });
    // Close dropdown when clicking outside
    document.addEventListener('click', (e) => {
        const container = document.getElementById('appDropdownContainer');
        if (container && !container.contains(e.target)) closeDropdown();
    });
});
loadConfig();
