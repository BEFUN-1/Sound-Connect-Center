#include "I18nManager.h"

namespace sony::devicecenter {

I18nManager& I18nManager::instance() {
    static I18nManager s_instance;
    return s_instance;
}

I18nManager::I18nManager() {
    _initTranslations();
}

QVariantList I18nManager::availableLanguages() const {
    QVariantList list;
    list.append(QVariantMap{{"code", "zh_CN"}, {"name", "简体中文"}});
    list.append(QVariantMap{{"code", "en"},    {"name", "English"}});
    list.append(QVariantMap{{"code", "pt_BR"}, {"name", "Português"}});
    list.append(QVariantMap{{"code", "es"},    {"name", "Español"}});
    list.append(QVariantMap{{"code", "de"},    {"name", "Deutsch"}});
    list.append(QVariantMap{{"code", "fr"},    {"name", "Français"}});
    list.append(QVariantMap{{"code", "ja"},    {"name", "日本語"}});
    return list;
}

QString I18nManager::translate(const QString& key, const QString& langCode) const {
    auto langIt = _strings.find(langCode);
    if (langIt != _strings.end()) {
        auto keyIt = langIt->find(key);
        if (keyIt != langIt->end()) {
            return *keyIt;
        }
    }
    // Fallback to English
    auto enIt = _strings.find("en");
    if (enIt != _strings.end()) {
        auto keyIt = enIt->find(key);
        if (keyIt != enIt->end()) {
            return *keyIt;
        }
    }
    return key;
}

void I18nManager::_initTranslations() {
    // English (en) - Source
    auto& en = _strings["en"];
    en["control_paused_system_connected"] = "System connected; control paused. Click Connect, or reconnect the headphones to this PC, to resume.";
    en["system_connected_control_paused"] = "System connected · Control paused";
    en["control_paused_short"] = "Control paused";
    en["waiting_for_power_on_short"] = "Waiting for power on";
    en["waiting_for_system_connection"] = "Waiting for Windows connection";
    en["control_recovering"] = "System connected · Restoring control";
    en["control_paused"] = "Control paused. Waiting for the headphones to reconnect to this PC, or click Connect.";
    en["waiting_for_power_on"] = "Waiting for the headphones to power on and reconnect to Windows";
    en["status_update_delayed"] = "Update delayed; showing last confirmed state";
    en["battery_last_updated"] = "Battery updated at %1";
    en["stat_last_mode"] = "Last confirmed mode";
    _strings["zh_CN"]["control_paused_system_connected"] = "系统已连接，控制已暂停；点击连接，或让耳机重新连接电脑后恢复";
    _strings["zh_CN"]["system_connected_control_paused"] = "系统已连接 · 控制已暂停";
    _strings["zh_CN"]["control_paused_short"] = "控制已暂停";
    _strings["zh_CN"]["waiting_for_power_on_short"] = "等待耳机开机";
    _strings["zh_CN"]["waiting_for_system_connection"] = "等待 Windows 连接";
    _strings["zh_CN"]["control_recovering"] = "系统已连接 · 正在恢复控制";
    _strings["zh_CN"]["control_paused"] = "控制已暂停；等待耳机重新连接电脑，也可点击连接";
    _strings["zh_CN"]["waiting_for_power_on"] = "等待耳机开机并重新连接 Windows";
    _strings["zh_CN"]["status_update_delayed"] = "更新延迟，显示最后确认的状态";
    _strings["zh_CN"]["battery_last_updated"] = "电量更新于 %1";
    _strings["zh_CN"]["stat_last_mode"] = "最后确认模式";
    en["nav_overview"]        = "Overview";
    en["nav_noise_control"]   = "Noise Control";
    en["nav_equalizer"]       = "Equalizer";
    en["nav_audio_features"]  = "Audio Features";
    en["nav_device_switcher"] = "Devices & Multipoint";
    en["nav_settings"]        = "Settings";

    en["settings_eyebrow"]    = "Preferences & Details";
    en["settings_title"]      = "Settings";
    en["settings_subtitle"]   = "System startup, language selection, application version, and community links.";
    en["system_preferences"]  = "System & Interface";
    en["init_with_os"]        = "Launch at System Startup";
    en["init_with_os_desc"]   = "Automatically launch Sony Device Center when you log in to your desktop.";
    en["language"]            = "Display Language";
    en["language_desc"]       = "Select your preferred language for the user interface.";
    en["theme"]               = "Theme";
    en["theme_desc"]          = "Choose a dark or light application theme.";
    en["dark_theme"]          = "Dark";
    en["light_theme"]         = "Light";
    en["about_app"]           = "About Application";
    en["disclaimer"]         = "Sony Device Center is an independent open-source project, free for anyone to use, study and modify. It is not affiliated with, endorsed by, or connected to Sony Corporation. Sony and all product names are trademarks of their respective owners.";
    en["app_version"]         = "Version";
    en["links_support"]       = "Community & Support";
    en["donate_desc"]         = "Enjoying Sony Device Center? Support the development or check out the open-source repository.";
    en["btn_github"]          = "GitHub";
    en["btn_donate"]          = "Donate";

    en["connected"]           = "Connected";
    en["disconnected"]        = "Disconnected";
    en["battery"]             = "Battery";
    en["charging"]            = "Charging";
    en["ready"]               = "Ready";
    en["noise_cancelling"]    = "Noise Cancelling";
    en["ambient_sound"]       = "Ambient Sound";
    en["noise_control_off"]   = "Off";
    en["ambient_level"]       = "Ambient Level";
    en["focus_on_voice"]      = "Focus on Voice";
    en["clear_bass"]          = "Clear Bass";
    en["active"]              = "Active";
    en["connect"]             = "Connect";
    en["available"]           = "Available";
    en["working"]             = "Working...";
    en["connection"]          = "Connection";
    en["remove_paired_device_title"] = "Remove paired device?";
    en["remove_paired_device_message"] = "Remove %1 from the headset's multipoint history?";
    en["rename_eq_preset_title"] = "Rename EQ preset";
    en["save_eq_preset_title"] = "Save EQ preset";
    en["rename_eq_preset_desc"] = "Choose a new name for this saved equalizer.";
    en["save_eq_preset_desc"] = "Save the current equalizer values as a reusable preset.";
    en["preset_name"]         = "Preset name";
    en["delete_eq_preset_title"] = "Delete EQ preset?";
    en["delete_eq_preset_message"] = "Delete \"%1\"? This cannot be undone.";
    en["overview_connected_device"] = "Connected Device";
    en["overview_offline"]    = "Offline";
    en["stat_codec"]          = "Codec";
    en["stat_battery"]        = "Battery";
    en["stat_mode"]           = "Mode";
    en["unknown"]             = "Unknown";
    en["mode_anc"]            = "ANC";
    en["mode_ambient"]        = "Ambient";
    en["mode_off"]            = "Off";
    en["processing_off"]      = "Processing Off";
    en["processing_off_desc"] = "Straight signal, no processing";
    en["noise_cancelling_desc"] = "The outside world is sealed out";
    en["ambient_sound_desc"]  = "Level %1 · hearing your surroundings";
    en["isolation"]           = "Isolation";
    en["noise_control"]       = "Noise Control";
    en["noise_control_subtitle"] = "Choose how much of the world gets through.";
    en["seal_out_room"]       = "Seal out the room";
    en["let_room_in"]         = "Let the room in";
    en["no_processing"]       = "No processing";
    en["ambient"]             = "Ambient";
    en["sound_level"]         = "Sound Level";
    en["focus_on_voice_desc"] = "Lift human speech, filter the low rumble";
    en["eq_prefix"]           = "EQ";
    en["signature"]           = "Signature";
    en["equalizer"]           = "Equalizer";
    en["eq_10band_label"]     = "10-Band";
    en["eq_5band_label"]      = "5-Band · ±10 dB";
    en["eq_10band_desc"]      = "Ten bands, fine-grained.";
    en["eq_5band_desc"]       = "Five bands, plus dedicated Clear Bass.";
    en["preset_off"]          = "Off";
    en["preset_bass_boost"]   = "Bass Boost";
    en["preset_treble_boost"] = "Treble Boost";
    en["preset_vocal"]        = "Vocal";
    en["preset_bright"]       = "Bright";
    en["preset_excited"]      = "Excited";
    en["preset_mellow"]       = "Mellow";
    en["preset_relaxed"]      = "Relaxed";
    en["preset_speech"]       = "Speech";
    en["preset_custom"]       = "Custom";
    en["my_presets"]          = "My presets";
    en["save_current"]        = "Save current";
    en["custom_current"]      = "Custom";
    en["clear_bass_desc"]     = "Sub-bass weight, no distortion";
    en["behaviour"]           = "Behaviour";
    en["audio_features_title"] = "Sound & Device Features";
    en["audio_features_subtitle"] = "Upscaling, speak-to-chat, and power discipline.";
    en["dsee_title"]          = "DSEE Extreme";
    en["dsee_desc"]           = "AI restores the highs that compression threw away";
    en["speak_title"]         = "Speak-to-Chat";
    en["speak_desc"]          = "When you speak, lowers or pauses music so you can hear the other person.";
    en["adaptive_title"]      = "Adaptive Volume";
    en["adaptive_desc"]       = "Balances level against your surroundings";
    en["not_supported"]       = "Not supported";
    en["state_unknown"]       = "State unknown — waiting for device";
    en["auto_power_off"]      = "Automatic Power Off";
    en["auto_power_off_desc"] = "Choose the automatic power-off condition supported by your headphones.";
    en["auto_power_off_removed_desc"] = "When enabled, turns off the headset after 15 minutes without being worn.";
    en["power_off"]           = "Power Off";
    en["power_off_desc"]      = "Turn off the connected headphones now";
    en["devices_eyebrow"]     = "Easy Switch";
    en["devices_title"]       = "Devices & Multipoint";
    en["devices_subtitle"]    = "Discover, switch, and manage the headphones' connected sources.";
    en["discovered_devices"]  = "Discovered devices";
    en["found"]               = "%1 found";
    en["refresh"]             = "Refresh";
    en["disconnect"]          = "Disconnect";
    en["headset_multipoint"]  = "Headset multipoint";
    en["sources_max"]         = "%1 sources · max %2";
    en["multipoint_not_supported"] = "Not supported";
    en["keep_source"]         = "Keep selected playback source";
    en["source_remembered"]   = "Headset remembers the active source";
    en["source_pinning_unsupported"] = "This headset does not expose source pinning";
    en["playing"]             = "Playing";
    en["paired_history"]      = "Paired history";
    en["set_default"]         = "Set default";
    en["play_here"]           = "Play here";
    en["unpair"]              = "Unpair";
    en["project_links"]       = "Project links";
    en["protocol_core"]       = "Protocol Core";
    en["framework"]           = "Framework";
    en["license"]             = "License";
    en["open_window"]         = "Open Sony Device Center";
    en["tray_noise_control"]  = "Noise Control";
    en["tray_noise_cancelling"] = "Noise Cancelling";
    en["tray_ambient_sound"]  = "Ambient Sound";
    en["tray_off"]            = "Off";
    en["tray_power_off"]      = "Power Off Headphones";
    en["tray_quit"]           = "Quit";
    en["tray_connect_first"]  = "Connect a supported headset first.";
    en["tray_battery"]        = "Sony Device Center · Battery %1";
    en["tray_battery_unknown"] = "Sony Device Center · Battery unknown";
    en["tray_disconnected"]   = "Sony Device Center · Disconnected";
    en["minutes"]             = "Minutes";
    en["hour"]                = "Hour";
    en["hours"]               = "Hours";
    en["power_off_when_removed"] = "When removed";

    // 简体中文（默认）
    auto& zh = _strings["zh_CN"];
    zh["nav_overview"]        = "概览";
    zh["nav_noise_control"]   = "降噪控制";
    zh["nav_equalizer"]       = "均衡器";
    zh["nav_audio_features"]  = "音频功能";
    zh["nav_device_switcher"] = "设备与多点";
    zh["nav_settings"]        = "设置";

    zh["settings_eyebrow"]    = "偏好与详情";
    zh["settings_title"]      = "设置";
    zh["settings_subtitle"]   = "系统启动、语言、主题、应用版本和社区链接。";
    zh["system_preferences"]  = "系统与界面";
    zh["init_with_os"]        = "开机时启动";
    zh["init_with_os_desc"]   = "登录桌面时自动启动 Sony Device Center。";
    zh["language"]            = "显示语言";
    zh["language_desc"]       = "选择应用界面使用的语言。";
    zh["theme"]               = "主题";
    zh["theme_desc"]          = "选择深色或浅色界面主题。";
    zh["dark_theme"]          = "深色";
    zh["light_theme"]         = "浅色";
    zh["about_app"]           = "关于应用";
    zh["disclaimer"]          = "Sony Device Center 是独立的开源项目，可自由使用、研究和修改。本项目与 Sony Corporation 没有关联，也未获得其认可或支持。Sony 及所有产品名称均为各自所有者的商标。";
    zh["app_version"]         = "版本";
    zh["links_support"]       = "社区与支持";
    zh["donate_desc"]         = "如果 Sony Device Center 对你有帮助，欢迎支持项目开发或访问开源仓库。";
    zh["btn_github"]          = "GitHub";
    zh["btn_donate"]          = "捐赠";

    zh["connected"]           = "已连接";
    zh["disconnected"]        = "未连接";
    zh["battery"]             = "电量";
    zh["charging"]            = "充电中";
    zh["ready"]               = "就绪";
    zh["noise_cancelling"]    = "降噪";
    zh["ambient_sound"]       = "环境声";
    zh["noise_control_off"]   = "关闭";
    zh["ambient_level"]       = "环境声等级";
    zh["focus_on_voice"]      = "语音增强";
    zh["clear_bass"]          = "低音增强";
    zh["active"]              = "启用";
    zh["connect"]             = "连接";
    zh["available"]           = "可用";
    zh["working"]             = "处理中...";
    zh["connection"]          = "连接状态";
    zh["remove_paired_device_title"] = "移除已配对设备？";
    zh["remove_paired_device_message"] = "要从耳机的多点历史记录中移除 %1 吗？";
    zh["rename_eq_preset_title"] = "重命名均衡器预设";
    zh["save_eq_preset_title"] = "保存均衡器预设";
    zh["rename_eq_preset_desc"] = "为已保存的均衡器预设选择新名称。";
    zh["save_eq_preset_desc"] = "将当前均衡器参数保存为可重复使用的预设。";
    zh["preset_name"]         = "预设名称";
    zh["delete_eq_preset_title"] = "删除均衡器预设？";
    zh["delete_eq_preset_message"] = "确定删除“%1”吗？此操作无法撤销。";
    zh["overview_connected_device"] = "已连接设备";
    zh["overview_offline"]    = "离线";
    zh["stat_codec"]          = "编解码器";
    zh["stat_battery"]        = "电量";
    zh["stat_mode"]           = "模式";
    zh["unknown"]             = "未知";
    zh["mode_anc"]            = "降噪";
    zh["mode_ambient"]        = "环境声";
    zh["mode_off"]            = "关闭";
    zh["processing_off"]      = "关闭处理";
    zh["processing_off_desc"] = "直通信号，不进行处理";
    zh["noise_cancelling_desc"] = "隔绝外部环境声音";
    zh["ambient_sound_desc"]  = "等级 %1 · 聆听周围环境";
    zh["isolation"]           = "隔离";
    zh["noise_control"]       = "降噪控制";
    zh["noise_control_subtitle"] = "选择让多少外部声音进入耳机。";
    zh["seal_out_room"]       = "隔绝环境声音";
    zh["let_room_in"]         = "引入环境声音";
    zh["no_processing"]       = "不进行处理";
    zh["ambient"]             = "环境声";
    zh["sound_level"]         = "环境声等级";
    zh["focus_on_voice_desc"] = "增强人声，过滤低频噪音";
    zh["eq_prefix"]           = "均衡器";
    zh["signature"]           = "音色";
    zh["equalizer"]           = "均衡器";
    zh["eq_10band_label"]     = "十段";
    zh["eq_5band_label"]      = "五段 · ±10 dB";
    zh["eq_10band_desc"]      = "十段均衡，精细调节。";
    zh["eq_5band_desc"]       = "五段均衡，另有独立低音增强。";
    zh["preset_off"]          = "关闭";
    zh["preset_bass_boost"]   = "低音增强";
    zh["preset_treble_boost"] = "高音增强";
    zh["preset_vocal"]        = "人声";
    zh["preset_bright"]       = "明亮";
    zh["preset_excited"]      = "激昂";
    zh["preset_mellow"]       = "柔和";
    zh["preset_relaxed"]      = "舒缓";
    zh["preset_speech"]       = "语音";
    zh["preset_custom"]       = "自定义";
    zh["my_presets"]          = "我的预设";
    zh["save_current"]        = "保存当前设置";
    zh["custom_current"]      = "自定义";
    zh["clear_bass_desc"]     = "增强超低音，同时避免失真";
    zh["behaviour"]           = "行为";
    zh["audio_features_title"] = "音频与设备功能";
    zh["audio_features_subtitle"] = "音质提升、对话感知和电源管理。";
    zh["dsee_title"]          = "DSEE Extreme";
    zh["dsee_desc"]           = "使用 AI 恢复压缩过程中损失的高频细节";
    zh["speak_title"]         = "智能免摘";
    zh["speak_desc"]          = "检测到您说话时，自动降低音量或暂停音乐，便于听清对方的声音。";
    zh["adaptive_title"]      = "自适应音量";
    zh["adaptive_desc"]       = "根据周围环境平衡音量";
    zh["not_supported"]       = "不支持";
    zh["state_unknown"]       = "状态未知，等待设备响应";
    zh["auto_power_off"]      = "自动关闭电源";
    zh["auto_power_off_desc"] = "选择耳机支持的自动关闭电源条件。";
    zh["auto_power_off_removed_desc"] = "开启后，未佩戴耳机 15 分钟将自动关闭电源。";
    zh["power_off"]           = "关闭耳机";
    zh["power_off_desc"]      = "立即关闭当前连接的耳机";
    zh["devices_eyebrow"]     = "快速切换";
    zh["devices_title"]       = "设备与多点";
    zh["devices_subtitle"]    = "发现、切换并管理耳机连接的音频来源。";
    zh["discovered_devices"]  = "发现的设备";
    zh["found"]               = "找到 %1 台";
    zh["refresh"]             = "刷新";
    zh["disconnect"]          = "断开连接";
    zh["headset_multipoint"]  = "耳机多点连接";
    zh["sources_max"]         = "%1 个来源 · 最多 %2 个";
    zh["multipoint_not_supported"] = "不支持";
    zh["keep_source"]         = "保持选定的播放来源";
    zh["source_remembered"]   = "耳机会记住当前活动来源";
    zh["source_pinning_unsupported"] = "此耳机不支持固定播放来源";
    zh["playing"]             = "正在播放";
    zh["paired_history"]      = "已配对历史";
    zh["set_default"]         = "设为默认";
    zh["play_here"]           = "在此播放";
    zh["unpair"]              = "取消配对";
    zh["project_links"]       = "项目链接";
    zh["protocol_core"]       = "协议核心";
    zh["framework"]           = "框架";
    zh["license"]             = "许可证";
    zh["open_window"]         = "打开主窗口";
    zh["tray_noise_control"]  = "降噪控制";
    zh["tray_noise_cancelling"] = "降噪";
    zh["tray_ambient_sound"]  = "环境声";
    zh["tray_off"]            = "关闭";
    zh["tray_power_off"]      = "关闭耳机";
    zh["tray_quit"]           = "退出";
    zh["tray_connect_first"]  = "请先连接支持的耳机。";
    zh["tray_battery"]        = "Sony Device Center · 电量 %1";
    zh["tray_battery_unknown"] = "Sony Device Center · 电量未知";
    zh["tray_disconnected"]   = "Sony Device Center · 未连接";
    zh["minutes"]             = "分钟";
    zh["hour"]                = "小时";
    zh["hours"]               = "小时";
    zh["power_off_when_removed"] = "摘下耳机";

    // Português (pt_BR)
    auto& pt = _strings["pt_BR"];
    pt["nav_overview"]        = "Visão Geral";
    pt["nav_noise_control"]   = "Controle de Ruído";
    pt["nav_equalizer"]       = "Equalizador";
    pt["nav_audio_features"]  = "Recursos de Áudio";
    pt["nav_device_switcher"] = "Alternar Dispositivo";
    pt["nav_settings"]        = "Configurações";

    pt["settings_eyebrow"]    = "Preferências & Detalhes";
    pt["settings_title"]      = "Configurações";
    pt["settings_subtitle"]   = "Inicialização com o sistema, idioma, versão do aplicativo e links da comunidade.";
    pt["system_preferences"]  = "Sistema & Interface";
    pt["init_with_os"]        = "Iniciar com o Sistema Operacional";
    pt["init_with_os_desc"]   = "Inicia o Sony Device Center automaticamente ao fazer login no seu computador.";
    pt["language"]            = "Idioma de Exibição";
    pt["language_desc"]       = "Selecione seu idioma preferido para a interface.";
    pt["about_app"]           = "Sobre o Aplicativo";
    pt["disclaimer"]         = "O Sony Device Center é um projeto open source independente, livre para qualquer pessoa usar, estudar e modificar. Não é afiliado, endossado nem conectado à Sony Corporation. Sony e todos os nomes de produtos são marcas de seus respectivos proprietários.";
    pt["app_version"]         = "Versão";
    pt["links_support"]       = "Comunidade & Apoio";
    pt["donate_desc"]         = "Gostando do Sony Device Center? Apoie o desenvolvimento contínuo ou confira o código-fonte.";
    pt["btn_github"]          = "GitHub";
    pt["btn_donate"]          = "Doar / Apoiar";

    pt["connected"]           = "Conectado";
    pt["disconnected"]        = "Desconectado";
    pt["battery"]             = "Bateria";
    pt["charging"]            = "Carregando";
    pt["ready"]               = "Pronto";
    pt["noise_cancelling"]    = "Cancelamento de Ruído";
    pt["ambient_sound"]       = "Som Ambiente";
    pt["noise_control_off"]   = "Desativado";
    pt["ambient_level"]       = "Nível Ambiente";
    pt["focus_on_voice"]      = "Foco na Voz";
    pt["clear_bass"]          = "Clear Bass";
    pt["active"]              = "Ativo";
    pt["connect"]             = "Conectar";
    pt["available"]           = "Disponível";

    // Español (es)
    auto& es = _strings["es"];
    es["nav_overview"]        = "Resumen";
    es["nav_noise_control"]   = "Control de Ruido";
    es["nav_equalizer"]       = "Ecualizador";
    es["nav_audio_features"]  = "Funciones de Audio";
    es["nav_device_switcher"] = "Cambiar Dispositivo";
    es["nav_settings"]        = "Ajustes";

    es["settings_eyebrow"]    = "Preferencias y Detalles";
    es["settings_title"]      = "Ajustes";
    es["settings_subtitle"]   = "Inicio con el sistema, idioma, versión de la aplicación y enlaces de la comunidad.";
    es["system_preferences"]  = "Sistema e Interfaz";
    es["init_with_os"]        = "Iniciar con el Sistema Operativo";
    es["init_with_os_desc"]   = "Inicia automáticamente Sony Device Center al iniciar sesión en su ordenador.";
    es["language"]            = "Idioma";
    es["language_desc"]       = "Seleccione su idioma preferido para la interfaz.";
    es["about_app"]           = "Acerca de la Aplicación";
    es["disclaimer"]         = "Sony Device Center es un proyecto de código abierto independiente, libre para que cualquiera lo use, estudie y modifique. No está afiliado, respaldado ni conectado con Sony Corporation. Sony y todos los nombres de productos son marcas de sus respectivos propietarios.";
    es["app_version"]         = "Versión";
    es["links_support"]       = "Comunidad y Soporte";
    es["donate_desc"]         = "¿Disfrutando de Sony Device Center? Apoye el desarrollo continuo o explore el código fuente.";
    es["btn_github"]          = "GitHub";
    es["btn_donate"]          = "Donar";

    es["connected"]           = "Conectado";
    es["disconnected"]        = "Desconectado";
    es["battery"]             = "Batería";
    es["charging"]            = "Cargando";
    es["ready"]               = "Listo";
    es["noise_cancelling"]    = "Cancelación de Ruido";
    es["ambient_sound"]       = "Sonido Ambiente";
    es["noise_control_off"]   = "Desactivado";
    es["ambient_level"]       = "Nivel Ambiente";
    es["focus_on_voice"]      = "Enfoque en Voz";
    es["clear_bass"]          = "Clear Bass";
    es["active"]              = "Activo";
    es["connect"]             = "Conectar";
    es["available"]           = "Disponible";

    // Deutsch (de)
    auto& de = _strings["de"];
    de["nav_overview"]        = "Übersicht";
    de["nav_noise_control"]   = "Geräuschkontrolle";
    de["nav_equalizer"]       = "Equalizer";
    de["nav_audio_features"]  = "Audiofunktionen";
    de["nav_device_switcher"] = "Geräte wechseln";
    de["nav_settings"]        = "Einstellungen";

    de["settings_eyebrow"]    = "Präferenzen & Details";
    de["settings_title"]      = "Einstellungen";
    de["settings_subtitle"]   = "Systemstart, Sprachauswahl, Anwendungsversion und Community-Links.";
    de["system_preferences"]  = "System & Oberfläche";
    de["init_with_os"]        = "Beim Systemstart ausführen";
    de["init_with_os_desc"]   = "Startet das Sony Device Center automatisch bei der Anmeldung am Desktop.";
    de["language"]            = "Sprache";
    de["language_desc"]       = "Wählen Sie Ihre bevorzugte Sprache für die Benutzeroberfläche.";
    de["about_app"]           = "Über die Anwendung";
    de["disclaimer"]         = "Sony Device Center ist ein unabhängiges Open-Source-Projekt, das jeder frei nutzen, untersuchen und verändern kann. Es steht in keiner Verbindung zur Sony Corporation und wird von ihr weder unterstützt noch empfohlen. Sony und alle Produktnamen sind Marken ihrer jeweiligen Inhaber.";
    de["app_version"]         = "Version";
    de["links_support"]       = "Community & Unterstützung";
    de["donate_desc"]         = "Gefällt Ihnen Sony Device Center? Unterstützen Sie die Weiterentwicklung oder besuchen Sie das Repository.";
    de["btn_github"]          = "GitHub";
    de["btn_donate"]          = "Spenden";

    de["connected"]           = "Verbunden";
    de["disconnected"]        = "Getrennt";
    de["battery"]             = "Akku";
    de["charging"]            = "Lädt";
    de["ready"]               = "Bereit";
    de["noise_cancelling"]    = "Geräuschminimierung";
    de["ambient_sound"]       = "Umgebungsklang";
    de["noise_control_off"]   = "Aus";
    de["ambient_level"]       = "Umgebungspegel";
    de["focus_on_voice"]      = "Fokus auf Stimme";
    de["clear_bass"]          = "Clear Bass";
    de["active"]              = "Aktiv";
    de["connect"]             = "Verbinden";
    de["available"]           = "Verfügbar";

    // Français (fr)
    auto& fr = _strings["fr"];
    fr["nav_overview"]        = "Aperçu";
    fr["nav_noise_control"]   = "Contrôle du Bruit";
    fr["nav_equalizer"]       = "Égaliseur";
    fr["nav_audio_features"]  = "Fonctions Audio";
    fr["nav_device_switcher"] = "Changer d'Appareil";
    fr["nav_settings"]        = "Paramètres";

    fr["settings_eyebrow"]    = "Préférences & Détails";
    fr["settings_title"]      = "Paramètres";
    fr["settings_subtitle"]   = "Démarrage système, langue, version de l'application et liens communautaires.";
    fr["system_preferences"]  = "Système & Interface";
    fr["init_with_os"]        = "Lancer au Démarrage du Système";
    fr["init_with_os_desc"]   = "Lance automatiquement Sony Device Center lors de la connexion à votre session.";
    fr["language"]            = "Langue d'Affichage";
    fr["language_desc"]       = "Sélectionnez votre langue préférée pour l'interface utilisateur.";
    fr["about_app"]           = "À propos de l'application";
    fr["disclaimer"]         = "Sony Device Center est un projet open source indépendant, que chacun est libre d'utiliser, d'étudier et de modifier. Il n'est ni affilié à Sony Corporation, ni approuvé ni soutenu par elle. Sony et tous les noms de produits sont des marques de leurs propriétaires respectifs.";
    fr["app_version"]         = "Version";
    fr["links_support"]       = "Communauté & Soutien";
    fr["donate_desc"]         = "Vous appréciez Sony Device Center ? Soutenez le développement continu ou consultez le code source.";
    fr["btn_github"]          = "GitHub";
    fr["btn_donate"]          = "Faire un don";

    fr["connected"]           = "Connecté";
    fr["disconnected"]        = "Déconnecté";
    fr["battery"]             = "Batterie";
    fr["charging"]            = "En charge";
    fr["ready"]               = "Prêt";
    fr["noise_cancelling"]    = "Réduction de bruit";
    fr["ambient_sound"]       = "Bruit ambiant";
    fr["noise_control_off"]   = "Désactivé";
    fr["ambient_level"]       = "Niveau ambiant";
    fr["focus_on_voice"]      = "Priorité à la voix";
    fr["clear_bass"]          = "Clear Bass";
    fr["active"]              = "Actif";
    fr["connect"]             = "Connecter";
    fr["available"]           = "Disponible";

    // 日本語 (ja)
    auto& ja = _strings["ja"];
    ja["nav_overview"]        = "概要";
    ja["nav_noise_control"]   = "ノイズコントロール";
    ja["nav_equalizer"]       = "イコライザー";
    ja["nav_audio_features"]  = "オーディオ機能";
    ja["nav_device_switcher"] = "デバイス切り替え";
    ja["nav_settings"]        = "設定";

    ja["settings_eyebrow"]    = "設定と情報";
    ja["settings_title"]      = "設定";
    ja["settings_subtitle"]   = "OS自動起動、言語設定、アプリのバージョン、コミュニティリンク。";
    ja["system_preferences"]  = "システムとインターフェース";
    ja["init_with_os"]        = "OS起動時に自動開始";
    ja["init_with_os_desc"]   = "ログイン時にSony Device Centerを自動的に起動します。";
    ja["language"]            = "表示言語";
    ja["language_desc"]       = "UIの表示言語を選択します。";
    ja["about_app"]           = "アプリについて";
    ja["disclaimer"]         = "Sony Device Center は独立したオープンソースプロジェクトであり、誰でも自由に使用・研究・改変できます。ソニー株式会社とは提携しておらず、承認や支援も受けていません。Sony および各製品名は各権利者の商標です。";
    ja["app_version"]         = "バージョン";
    ja["links_support"]       = "コミュニティと支援";
    ja["donate_desc"]         = "Sony Device Centerを気に入っていただけましたら、開発のご支援やリポジトリの確認をお願いします。";
    ja["btn_github"]          = "GitHub";
    ja["btn_donate"]          = "支援・寄付";

    ja["connected"]           = "接続済み";
    ja["disconnected"]        = "未接続";
    ja["battery"]             = "バッテリー";
    ja["charging"]            = "充電中";
    ja["ready"]               = "準備完了";
    ja["noise_cancelling"]    = "ノイズキャンセリング";
    ja["ambient_sound"]       = "外音取り込み (アンビエント)";
    ja["noise_control_off"]   = "オフ";
    ja["ambient_level"]       = "外音取り込みレベル";
    ja["focus_on_voice"]      = "ボイスフォーカス";
    ja["clear_bass"]          = "クリアベース";
    ja["active"]              = "接続中";
    ja["connect"]             = "接続";
    ja["available"]           = "利用可能";
    // Complete the feature vocabulary for every selectable locale so new pages do not fall back to English.
    const QMap<QString, QMap<QString, QString>> additionalTranslations = {
        {"pt_BR", {
            {"adaptive_desc", "Equilibra o volume com o ambiente"},
            {"adaptive_title", "Volume Adaptativo"},
            {"ambient", "Ambiente"},
            {"ambient_sound_desc", "Nível %1 · ouvindo o ambiente"},
            {"audio_features_subtitle", "Qualidade, conversa e gestão de energia."},
            {"audio_features_title", "Recursos de Som e Dispositivo"},
            {"auto_power_off", "Desligamento Automático"},
            {"auto_power_off_desc", "Desliga após inatividade ou remoção"},
            {"auto_power_off_removed_desc", "Quando ativado, os fones desligam algum tempo após serem removidos."},
            {"behaviour", "Comportamento"},
            {"clear_bass_desc", "Graves profundos sem distorção"},
            {"connection", "Conexão"},
            {"dark_theme", "Escuro"},
            {"delete_eq_preset_message", "Excluir \"%1\"? Esta ação não pode ser desfeita."},
            {"delete_eq_preset_title", "Excluir predefinição de EQ?"},
            {"devices_eyebrow", "Troca Rápida"},
            {"devices_subtitle", "Descubra, alterne e gerencie as fontes conectadas aos fones."},
            {"devices_title", "Dispositivos e Multiponto"},
            {"disconnect", "Desconectar"},
            {"discovered_devices", "Dispositivos encontrados"},
            {"dsee_desc", "A IA recupera os agudos perdidos na compressão"},
            {"dsee_title", "DSEE Extreme"},
            {"eq_10band_desc", "Dez bandas para ajustes precisos."},
            {"eq_5band_desc", "Cinco bandas e Clear Bass dedicado."},
            {"eq_10band_label", "10 bandas"},
            {"eq_5band_label", "5 bandas · ±10 dB"},
            {"eq_prefix", "EQ"},
            {"equalizer", "Equalizador"},
            {"focus_on_voice_desc", "Realça vozes e filtra o ruído grave"},
            {"found", "%1 encontrados"},
            {"framework", "Framework"},
            {"headset_multipoint", "Multiponto dos fones"},
            {"hour", "hora"},
            {"hours", "horas"},
            {"isolation", "Isolamento"},
            {"keep_source", "Manter a fonte de reprodução selecionada"},
            {"let_room_in", "Deixar o ambiente entrar"},
            {"license", "Licença"},
            {"light_theme", "Claro"},
            {"minutes", "minutos"},
            {"mode_ambient", "Ambiente"},
            {"mode_anc", "ANC"},
            {"mode_off", "Desativado"},
            {"multipoint_not_supported", "Não suportado"},
            {"my_presets", "Minhas predefinições"},
            {"no_processing", "Sem processamento"},
            {"noise_cancelling_desc", "Bloqueia os sons externos"},
            {"noise_control", "Controle de Ruído"},
            {"noise_control_subtitle", "Escolha quanto do ambiente deve passar."},
            {"not_supported", "Não suportado"},
            {"overview_connected_device", "Dispositivo conectado"},
            {"overview_offline", "Offline"},
            {"paired_history", "Histórico de pareamento"},
            {"play_here", "Reproduzir aqui"},
            {"playing", "Reproduzindo"},
            {"power_off", "Desligar fones"},
            {"power_off_desc", "Desliga os fones conectados agora"},
            {"power_off_when_removed", "Ao remover"},
            {"preset_bass_boost", "Reforço de graves"},
            {"preset_bright", "Brilhante"},
            {"preset_custom", "Personalizado"},
            {"preset_excited", "Animado"},
            {"preset_mellow", "Suave"},
            {"preset_name", "Nome da predefinição"},
            {"preset_off", "Desativado"},
            {"preset_relaxed", "Relaxado"},
            {"preset_speech", "Voz"},
            {"preset_treble_boost", "Reforço de agudos"},
            {"preset_vocal", "Vocal"},
            {"processing_off", "Processamento desativado"},
            {"processing_off_desc", "Sinal direto, sem processamento"},
            {"project_links", "Links do projeto"},
            {"protocol_core", "Núcleo do protocolo"},
            {"refresh", "Atualizar"},
            {"remove_paired_device_message", "Remover %1 do histórico de multiponto dos fones?"},
            {"remove_paired_device_title", "Remover dispositivo pareado?"},
            {"rename_eq_preset_desc", "Escolha um novo nome para esta predefinição."},
            {"rename_eq_preset_title", "Renomear predefinição de EQ"},
            {"save_current", "Salvar atual"},
            {"save_eq_preset_desc", "Salve os valores atuais como uma predefinição reutilizável."},
            {"save_eq_preset_title", "Salvar predefinição de EQ"},
            {"seal_out_room", "Bloquear o ambiente"},
            {"set_default", "Definir padrão"},
            {"signature", "Assinatura"},
            {"sound_level", "Nível do som"},
            {"source_pinning_unsupported", "Este fone não oferece fixação de fonte"},
            {"source_remembered", "Os fones lembram a fonte ativa"},
            {"sources_max", "%1 fontes · máximo %2"},
            {"speak_desc", "Pausa a reprodução e abre o ambiente quando você fala"},
            {"speak_title", "Falar para Conversar"},
            {"stat_battery", "Bateria"},
            {"stat_codec", "Codec"},
            {"stat_mode", "Modo"},
            {"state_unknown", "Estado desconhecido · aguardando o dispositivo"},
            {"theme", "Tema"},
            {"theme_desc", "Escolha um tema escuro ou claro."},
            {"unknown", "Desconhecido"},
            {"unpair", "Desparear"},
            {"working", "Processando..."},
            {"open_window", "Abrir Sony Device Center"},
            {"tray_noise_control", "Controle de Ruído"},
            {"tray_noise_cancelling", "Cancelamento de Ruído"},
            {"tray_ambient_sound", "Som Ambiente"},
            {"tray_off", "Desativado"},
            {"tray_power_off", "Desligar Fones"},
            {"tray_quit", "Sair"},
            {"tray_connect_first", "Conecte um fone compatível primeiro."},
            {"tray_battery", "Sony Device Center · Bateria %1"},
            {"tray_battery_unknown", "Sony Device Center · Bateria desconhecida"},
            {"tray_disconnected", "Sony Device Center · Desconectado"}
        }},
        {"es", {
            {"adaptive_desc", "Equilibra el volumen según el entorno"},
            {"adaptive_title", "Volumen Adaptativo"},
            {"ambient", "Ambiente"},
            {"ambient_sound_desc", "Nivel %1 · escuchando el entorno"},
            {"audio_features_subtitle", "Mejora de sonido, conversación y energía."},
            {"audio_features_title", "Funciones de Sonido y Dispositivo"},
            {"auto_power_off", "Apagado Automático"},
            {"auto_power_off_desc", "Apaga tras inactividad o al quitarlo"},
            {"auto_power_off_removed_desc", "Al activarlo, los auriculares se apagan un tiempo después de quitártelos."},
            {"behaviour", "Comportamiento"},
            {"clear_bass_desc", "Graves profundos sin distorsión"},
            {"connection", "Conexión"},
            {"dark_theme", "Oscuro"},
            {"delete_eq_preset_message", "¿Eliminar \"%1\"? No se puede deshacer."},
            {"delete_eq_preset_title", "¿Eliminar preajuste de EQ?"},
            {"devices_eyebrow", "Cambio Rápido"},
            {"devices_subtitle", "Descubre, cambia y gestiona las fuentes conectadas a los auriculares."},
            {"devices_title", "Dispositivos y Multipunto"},
            {"disconnect", "Desconectar"},
            {"discovered_devices", "Dispositivos encontrados"},
            {"dsee_desc", "La IA recupera los agudos perdidos por la compresión"},
            {"dsee_title", "DSEE Extreme"},
            {"eq_10band_desc", "Diez bandas para ajustes precisos."},
            {"eq_5band_desc", "Cinco bandas y Clear Bass independiente."},
            {"eq_10band_label", "10 bandas"},
            {"eq_5band_label", "5 bandas · ±10 dB"},
            {"eq_prefix", "EQ"},
            {"equalizer", "Ecualizador"},
            {"focus_on_voice_desc", "Realza las voces y filtra el ruido grave"},
            {"found", "%1 encontrados"},
            {"framework", "Framework"},
            {"headset_multipoint", "Multipunto de los auriculares"},
            {"hour", "hora"},
            {"hours", "horas"},
            {"isolation", "Aislamiento"},
            {"keep_source", "Mantener la fuente de reproducción seleccionada"},
            {"let_room_in", "Dejar entrar el entorno"},
            {"license", "Licencia"},
            {"light_theme", "Claro"},
            {"minutes", "minutos"},
            {"mode_ambient", "Ambiente"},
            {"mode_anc", "ANC"},
            {"mode_off", "Desactivado"},
            {"multipoint_not_supported", "No compatible"},
            {"my_presets", "Mis preajustes"},
            {"no_processing", "Sin procesamiento"},
            {"noise_cancelling_desc", "Bloquea los sonidos externos"},
            {"noise_control", "Control de Ruido"},
            {"noise_control_subtitle", "Elige cuánto sonido del entorno debe entrar."},
            {"not_supported", "No compatible"},
            {"overview_connected_device", "Dispositivo conectado"},
            {"overview_offline", "Sin conexión"},
            {"paired_history", "Historial de emparejamiento"},
            {"play_here", "Reproducir aquí"},
            {"playing", "Reproduciendo"},
            {"power_off", "Apagar auriculares"},
            {"power_off_desc", "Apaga ahora los auriculares conectados"},
            {"power_off_when_removed", "Al quitarlo"},
            {"preset_bass_boost", "Refuerzo de graves"},
            {"preset_bright", "Brillante"},
            {"preset_custom", "Personalizado"},
            {"preset_excited", "Excitado"},
            {"preset_mellow", "Suave"},
            {"preset_name", "Nombre del preajuste"},
            {"preset_off", "Desactivado"},
            {"preset_relaxed", "Relajado"},
            {"preset_speech", "Voz"},
            {"preset_treble_boost", "Refuerzo de agudos"},
            {"preset_vocal", "Vocal"},
            {"processing_off", "Procesamiento desactivado"},
            {"processing_off_desc", "Señal directa, sin procesamiento"},
            {"project_links", "Enlaces del proyecto"},
            {"protocol_core", "Núcleo del protocolo"},
            {"refresh", "Actualizar"},
            {"remove_paired_device_message", "¿Quitar %1 del historial multipunto de los auriculares?"},
            {"remove_paired_device_title", "¿Quitar dispositivo emparejado?"},
            {"rename_eq_preset_desc", "Elige un nuevo nombre para este preajuste."},
            {"rename_eq_preset_title", "Renombrar preajuste de EQ"},
            {"save_current", "Guardar actual"},
            {"save_eq_preset_desc", "Guarda los valores actuales como un preajuste reutilizable."},
            {"save_eq_preset_title", "Guardar preajuste de EQ"},
            {"seal_out_room", "Aislar el entorno"},
            {"set_default", "Establecer predeterminado"},
            {"signature", "Firma"},
            {"sound_level", "Nivel de sonido"},
            {"source_pinning_unsupported", "Este auricular no permite fijar la fuente"},
            {"source_remembered", "Los auriculares recuerdan la fuente activa"},
            {"sources_max", "%1 fuentes · máximo %2"},
            {"speak_desc", "Pausa la reproducción y abre el ambiente al hablar"},
            {"speak_title", "Hablar para conversar"},
            {"stat_battery", "Batería"},
            {"stat_codec", "Códec"},
            {"stat_mode", "Modo"},
            {"state_unknown", "Estado desconocido · esperando al dispositivo"},
            {"theme", "Tema"},
            {"theme_desc", "Elige un tema oscuro o claro."},
            {"unknown", "Desconocido"},
            {"unpair", "Desvincular"},
            {"working", "Procesando..."},
            {"open_window", "Abrir Sony Device Center"},
            {"tray_noise_control", "Control de Ruido"},
            {"tray_noise_cancelling", "Cancelación de Ruido"},
            {"tray_ambient_sound", "Sonido Ambiente"},
            {"tray_off", "Desactivado"},
            {"tray_power_off", "Apagar auriculares"},
            {"tray_quit", "Salir"},
            {"tray_connect_first", "Conecte primero unos auriculares compatibles."},
            {"tray_battery", "Sony Device Center · Batería %1"},
            {"tray_battery_unknown", "Sony Device Center · Batería desconocida"},
            {"tray_disconnected", "Sony Device Center · Desconectado"}
        }},
        {"de", {
            {"adaptive_desc", "Passt die Lautstärke an die Umgebung an"},
            {"adaptive_title", "Adaptive Lautstärke"},
            {"ambient", "Umgebung"},
            {"ambient_sound_desc", "Stufe %1 · Umgebung hören"},
            {"audio_features_subtitle", "Klangverbesserung, Gesprächserkennung und Energie."},
            {"audio_features_title", "Klang- und Gerätefunktionen"},
            {"auto_power_off", "Automatisches Ausschalten"},
            {"auto_power_off_desc", "Nach Leerlauf oder Abnehmen ausschalten"},
            {"auto_power_off_removed_desc", "Wenn aktiviert, schaltet sich der Kopfhörer einige Zeit nach dem Abnehmen aus."},
            {"behaviour", "Verhalten"},
            {"clear_bass_desc", "Mehr Tiefbass ohne Verzerrung"},
            {"connection", "Verbindung"},
            {"dark_theme", "Dunkel"},
            {"delete_eq_preset_message", "\"%1\" löschen? Dies kann nicht rückgängig gemacht werden."},
            {"delete_eq_preset_title", "EQ-Voreinstellung löschen?"},
            {"devices_eyebrow", "Schnellwechsel"},
            {"devices_subtitle", "Verbundene Quellen der Kopfhörer finden, wechseln und verwalten."},
            {"devices_title", "Geräte und Multipoint"},
            {"disconnect", "Trennen"},
            {"discovered_devices", "Gefundene Geräte"},
            {"dsee_desc", "KI stellt durch Kompression verlorene Höhen wieder her"},
            {"dsee_title", "DSEE Extreme"},
            {"eq_10band_desc", "Zehn Bänder für feine Anpassungen."},
            {"eq_5band_desc", "Fünf Bänder plus Clear Bass."},
            {"eq_10band_label", "10-Band"},
            {"eq_5band_label", "5-Band · ±10 dB"},
            {"eq_prefix", "EQ"},
            {"equalizer", "Equalizer"},
            {"focus_on_voice_desc", "Hebt Stimmen an und filtert tiefe Geräusche"},
            {"found", "%1 gefunden"},
            {"framework", "Framework"},
            {"headset_multipoint", "Kopfhörer-Multipoint"},
            {"hour", "Stunde"},
            {"hours", "Stunden"},
            {"isolation", "Isolation"},
            {"keep_source", "Ausgewählte Wiedergabequelle beibehalten"},
            {"let_room_in", "Umgebung hereinlassen"},
            {"license", "Lizenz"},
            {"light_theme", "Hell"},
            {"minutes", "Minuten"},
            {"mode_ambient", "Umgebung"},
            {"mode_anc", "ANC"},
            {"mode_off", "Aus"},
            {"multipoint_not_supported", "Nicht unterstützt"},
            {"my_presets", "Meine Voreinstellungen"},
            {"no_processing", "Keine Verarbeitung"},
            {"noise_cancelling_desc", "Hält Außengeräusche fern"},
            {"noise_control", "Geräuschkontrolle"},
            {"noise_control_subtitle", "Wähle, wie viel Umgebung durchgelassen wird."},
            {"not_supported", "Nicht unterstützt"},
            {"overview_connected_device", "Verbundenes Gerät"},
            {"overview_offline", "Offline"},
            {"paired_history", "Kopplungsverlauf"},
            {"play_here", "Hier wiedergeben"},
            {"playing", "Wiedergabe"},
            {"power_off", "Kopfhörer ausschalten"},
            {"power_off_desc", "Verbundene Kopfhörer sofort ausschalten"},
            {"power_off_when_removed", "Beim Abnehmen"},
            {"preset_bass_boost", "Bassverstärkung"},
            {"preset_bright", "Hell"},
            {"preset_custom", "Benutzerdefiniert"},
            {"preset_excited", "Lebhaft"},
            {"preset_mellow", "Sanft"},
            {"preset_name", "Name der Voreinstellung"},
            {"preset_off", "Aus"},
            {"preset_relaxed", "Entspannt"},
            {"preset_speech", "Sprache"},
            {"preset_treble_boost", "Höhenverstärkung"},
            {"preset_vocal", "Vokal"},
            {"processing_off", "Verarbeitung aus"},
            {"processing_off_desc", "Direktsignal ohne Verarbeitung"},
            {"project_links", "Projektlinks"},
            {"protocol_core", "Protokollkern"},
            {"refresh", "Aktualisieren"},
            {"remove_paired_device_message", "%1 aus dem Multipoint-Verlauf der Kopfhörer entfernen?"},
            {"remove_paired_device_title", "Gekoppeltes Gerät entfernen?"},
            {"rename_eq_preset_desc", "Wähle einen neuen Namen für diese Voreinstellung."},
            {"rename_eq_preset_title", "EQ-Voreinstellung umbenennen"},
            {"save_current", "Aktuelle speichern"},
            {"save_eq_preset_desc", "Aktuelle Werte als wiederverwendbare Voreinstellung speichern."},
            {"save_eq_preset_title", "EQ-Voreinstellung speichern"},
            {"seal_out_room", "Umgebung abschirmen"},
            {"set_default", "Als Standard festlegen"},
            {"signature", "Klangbild"},
            {"sound_level", "Klangpegel"},
            {"source_pinning_unsupported", "Diese Kopfhörer unterstützen keine Quellenfixierung"},
            {"source_remembered", "Die Kopfhörer merken sich die aktive Quelle"},
            {"sources_max", "%1 Quellen · max. %2"},
            {"speak_desc", "Pausiert die Wiedergabe und öffnet die Umgebung beim Sprechen"},
            {"speak_title", "Sprechautomatik"},
            {"stat_battery", "Akku"},
            {"stat_codec", "Codec"},
            {"stat_mode", "Modus"},
            {"state_unknown", "Unbekannter Zustand · auf Gerät warten"},
            {"theme", "Design"},
            {"theme_desc", "Wähle ein dunkles oder helles Design."},
            {"unknown", "Unbekannt"},
            {"unpair", "Kopplung aufheben"},
            {"working", "Wird verarbeitet..."},
            {"open_window", "Sony Device Center öffnen"},
            {"tray_noise_control", "Geräuschkontrolle"},
            {"tray_noise_cancelling", "Geräuschminimierung"},
            {"tray_ambient_sound", "Umgebungsklang"},
            {"tray_off", "Aus"},
            {"tray_power_off", "Kopfhörer ausschalten"},
            {"tray_quit", "Beenden"},
            {"tray_connect_first", "Verbinde zuerst ein unterstütztes Headset."},
            {"tray_battery", "Sony Device Center · Akku %1"},
            {"tray_battery_unknown", "Sony Device Center · Akku unbekannt"},
            {"tray_disconnected", "Sony Device Center · Getrennt"}
        }},
        {"fr", {
            {"adaptive_desc", "Équilibre le volume selon l’environnement"},
            {"adaptive_title", "Volume adaptatif"},
            {"ambient", "Ambiant"},
            {"ambient_sound_desc", "Niveau %1 · écoute de l’environnement"},
            {"audio_features_subtitle", "Amélioration du son, conversation et énergie."},
            {"audio_features_title", "Fonctions audio et appareil"},
            {"auto_power_off", "Mise hors tension automatique"},
            {"auto_power_off_desc", "Éteindre après inactivité ou retrait"},
            {"auto_power_off_removed_desc", "Lorsque cette option est activée, le casque s’éteint quelque temps après son retrait."},
            {"behaviour", "Comportement"},
            {"clear_bass_desc", "Des graves profonds sans distorsion"},
            {"connection", "Connexion"},
            {"dark_theme", "Sombre"},
            {"delete_eq_preset_message", "Supprimer « %1 » ? Cette action est irréversible."},
            {"delete_eq_preset_title", "Supprimer le préréglage EQ ?"},
            {"devices_eyebrow", "Changement rapide"},
            {"devices_subtitle", "Découvrez, changez et gérez les sources connectées au casque."},
            {"devices_title", "Appareils et multipoint"},
            {"disconnect", "Déconnecter"},
            {"discovered_devices", "Appareils trouvés"},
            {"dsee_desc", "L’IA restaure les aigus perdus par la compression"},
            {"dsee_title", "DSEE Extreme"},
            {"eq_10band_desc", "Dix bandes pour un réglage précis."},
            {"eq_5band_desc", "Cinq bandes et Clear Bass dédié."},
            {"eq_10band_label", "10 bandes"},
            {"eq_5band_label", "5 bandes · ±10 dB"},
            {"eq_prefix", "EQ"},
            {"equalizer", "Égaliseur"},
            {"focus_on_voice_desc", "Accentue les voix et filtre les graves"},
            {"found", "%1 trouvés"},
            {"framework", "Framework"},
            {"headset_multipoint", "Multipoint du casque"},
            {"hour", "heure"},
            {"hours", "heures"},
            {"isolation", "Isolation"},
            {"keep_source", "Conserver la source de lecture sélectionnée"},
            {"let_room_in", "Laisser entrer l’environnement"},
            {"license", "Licence"},
            {"light_theme", "Clair"},
            {"minutes", "minutes"},
            {"mode_ambient", "Ambiant"},
            {"mode_anc", "ANC"},
            {"mode_off", "Désactivé"},
            {"multipoint_not_supported", "Non pris en charge"},
            {"my_presets", "Mes préréglages"},
            {"no_processing", "Sans traitement"},
            {"noise_cancelling_desc", "Bloque les sons extérieurs"},
            {"noise_control", "Contrôle du bruit"},
            {"noise_control_subtitle", "Choisissez la quantité de sons extérieurs admise."},
            {"not_supported", "Non pris en charge"},
            {"overview_connected_device", "Appareil connecté"},
            {"overview_offline", "Hors ligne"},
            {"paired_history", "Historique des associations"},
            {"play_here", "Lire ici"},
            {"playing", "Lecture"},
            {"power_off", "Éteindre le casque"},
            {"power_off_desc", "Éteint maintenant le casque connecté"},
            {"power_off_when_removed", "Au retrait"},
            {"preset_bass_boost", "Renforcement des graves"},
            {"preset_bright", "Clair"},
            {"preset_custom", "Personnalisé"},
            {"preset_excited", "Dynamique"},
            {"preset_mellow", "Doux"},
            {"preset_name", "Nom du préréglage"},
            {"preset_off", "Désactivé"},
            {"preset_relaxed", "Détendu"},
            {"preset_speech", "Voix"},
            {"preset_treble_boost", "Renforcement des aigus"},
            {"preset_vocal", "Vocal"},
            {"processing_off", "Traitement désactivé"},
            {"processing_off_desc", "Signal direct, sans traitement"},
            {"project_links", "Liens du projet"},
            {"protocol_core", "Cœur du protocole"},
            {"refresh", "Actualiser"},
            {"remove_paired_device_message", "Retirer %1 de l’historique multipoint du casque ?"},
            {"remove_paired_device_title", "Retirer l’appareil associé ?"},
            {"rename_eq_preset_desc", "Choisissez un nouveau nom pour ce préréglage."},
            {"rename_eq_preset_title", "Renommer le préréglage EQ"},
            {"save_current", "Enregistrer actuel"},
            {"save_eq_preset_desc", "Enregistrez les valeurs actuelles comme préréglage réutilisable."},
            {"save_eq_preset_title", "Enregistrer le préréglage EQ"},
            {"seal_out_room", "Isoler la pièce"},
            {"set_default", "Définir par défaut"},
            {"signature", "Signature"},
            {"sound_level", "Niveau sonore"},
            {"source_pinning_unsupported", "Ce casque ne permet pas de fixer une source"},
            {"source_remembered", "Le casque mémorise la source active"},
            {"sources_max", "%1 sources · max. %2"},
            {"speak_desc", "Met en pause et ouvre le son ambiant quand vous parlez"},
            {"speak_title", "Détection de la parole"},
            {"stat_battery", "Batterie"},
            {"stat_codec", "Codec"},
            {"stat_mode", "Mode"},
            {"state_unknown", "État inconnu · en attente de l’appareil"},
            {"theme", "Thème"},
            {"theme_desc", "Choisissez un thème sombre ou clair."},
            {"unknown", "Inconnu"},
            {"unpair", "Dissocier"},
            {"working", "Traitement..."},
            {"open_window", "Ouvrir Sony Device Center"},
            {"tray_noise_control", "Contrôle du bruit"},
            {"tray_noise_cancelling", "Réduction de bruit"},
            {"tray_ambient_sound", "Son ambiant"},
            {"tray_off", "Désactivé"},
            {"tray_power_off", "Éteindre le casque"},
            {"tray_quit", "Quitter"},
            {"tray_connect_first", "Connectez d'abord un casque pris en charge."},
            {"tray_battery", "Sony Device Center · Batterie %1"},
            {"tray_battery_unknown", "Sony Device Center · Batterie inconnue"},
            {"tray_disconnected", "Sony Device Center · Déconnecté"}
        }},
        {"ja", {
            {"adaptive_desc", "周囲の環境に合わせて音量を調整します"},
            {"adaptive_title", "アダプティブ音量"},
            {"ambient", "アンビエント"},
            {"ambient_sound_desc", "レベル %1 · 周囲の音を聴く"},
            {"audio_features_subtitle", "音質向上、会話検出、電源管理。"},
            {"audio_features_title", "サウンドとデバイス機能"},
            {"auto_power_off", "自動電源オフ"},
            {"auto_power_off_desc", "無操作または取り外し後に電源を切ります"},
            {"auto_power_off_removed_desc", "オンにすると、ヘッドホンを外してしばらくすると自動で電源が切れます。"},
            {"behaviour", "動作"},
            {"clear_bass_desc", "歪みのない低音域の強調"},
            {"connection", "接続"},
            {"dark_theme", "ダーク"},
            {"delete_eq_preset_message", "「%1」を削除しますか？元に戻せません。"},
            {"delete_eq_preset_title", "EQプリセットを削除しますか？"},
            {"devices_eyebrow", "かんたん切り替え"},
            {"devices_subtitle", "ヘッドホンに接続された音源を検出、切り替え、管理します。"},
            {"devices_title", "デバイスとマルチポイント"},
            {"disconnect", "切断"},
            {"discovered_devices", "検出されたデバイス"},
            {"dsee_desc", "圧縮で失われた高音域をAIで復元します"},
            {"dsee_title", "DSEE Extreme"},
            {"eq_10band_desc", "10バンドで細かく調整できます。"},
            {"eq_5band_desc", "5バンドと専用Clear Bass。"},
            {"eq_10band_label", "10バンド"},
            {"eq_5band_label", "5バンド · ±10 dB"},
            {"eq_prefix", "EQ"},
            {"equalizer", "イコライザー"},
            {"focus_on_voice_desc", "人の声を強調し低い騒音を抑えます"},
            {"found", "%1件"},
            {"framework", "フレームワーク"},
            {"headset_multipoint", "ヘッドホンのマルチポイント"},
            {"hour", "時間"},
            {"hours", "時間"},
            {"isolation", "遮音"},
            {"keep_source", "選択した再生ソースを保持"},
            {"let_room_in", "外音を取り込む"},
            {"license", "ライセンス"},
            {"light_theme", "ライト"},
            {"minutes", "分"},
            {"mode_ambient", "アンビエント"},
            {"mode_anc", "ノイズキャンセリング"},
            {"mode_off", "オフ"},
            {"multipoint_not_supported", "非対応"},
            {"my_presets", "マイプリセット"},
            {"no_processing", "処理なし"},
            {"noise_cancelling_desc", "外部の音を遮断します"},
            {"noise_control", "ノイズコントロール"},
            {"noise_control_subtitle", "取り込む外音の量を選択します。"},
            {"not_supported", "非対応"},
            {"overview_connected_device", "接続中のデバイス"},
            {"overview_offline", "オフライン"},
            {"paired_history", "ペアリング履歴"},
            {"play_here", "ここで再生"},
            {"playing", "再生中"},
            {"power_off", "ヘッドホンの電源オフ"},
            {"power_off_desc", "接続中のヘッドホンの電源を今すぐ切ります"},
            {"power_off_when_removed", "装着を外したとき"},
            {"preset_bass_boost", "低音ブースト"},
            {"preset_bright", "ブライト"},
            {"preset_custom", "カスタム"},
            {"preset_excited", "エキサイト"},
            {"preset_mellow", "メロウ"},
            {"preset_name", "プリセット名"},
            {"preset_off", "オフ"},
            {"preset_relaxed", "リラックス"},
            {"preset_speech", "スピーチ"},
            {"preset_treble_boost", "高音ブースト"},
            {"preset_vocal", "ボーカル"},
            {"processing_off", "処理オフ"},
            {"processing_off_desc", "処理なしのダイレクト信号"},
            {"project_links", "プロジェクトリンク"},
            {"protocol_core", "プロトコルコア"},
            {"refresh", "更新"},
            {"remove_paired_device_message", "ヘッドホンのマルチポイント履歴から%1を削除しますか？"},
            {"remove_paired_device_title", "ペアリング済みデバイスを削除しますか？"},
            {"rename_eq_preset_desc", "保存済みEQの新しい名前を選択します。"},
            {"rename_eq_preset_title", "EQプリセット名を変更"},
            {"save_current", "現在の設定を保存"},
            {"save_eq_preset_desc", "現在のEQ値を再利用できるプリセットとして保存します。"},
            {"save_eq_preset_title", "EQプリセットを保存"},
            {"seal_out_room", "外音を遮断"},
            {"set_default", "既定に設定"},
            {"signature", "音色"},
            {"sound_level", "音量レベル"},
            {"source_pinning_unsupported", "このヘッドホンは再生ソースの固定に対応していません"},
            {"source_remembered", "ヘッドホンがアクティブなソースを記憶します"},
            {"sources_max", "%1ソース · 最大%2"},
            {"speak_desc", "話すと再生を一時停止しアンビエントに切り替えます"},
            {"speak_title", "スピーク・トゥ・チャット"},
            {"stat_battery", "バッテリー"},
            {"stat_codec", "コーデック"},
            {"stat_mode", "モード"},
            {"state_unknown", "状態不明 · デバイスを待機中"},
            {"theme", "テーマ"},
            {"theme_desc", "ダークまたはライトのテーマを選択します。"},
            {"unknown", "不明"},
            {"unpair", "ペアリング解除"},
            {"working", "処理中..."},
            {"open_window", "Sony Device Centerを開く"},
            {"tray_noise_control", "ノイズコントロール"},
            {"tray_noise_cancelling", "ノイズキャンセリング"},
            {"tray_ambient_sound", "外音取り込み"},
            {"tray_off", "オフ"},
            {"tray_power_off", "ヘッドホンの電源オフ"},
            {"tray_quit", "終了"},
            {"tray_connect_first", "対応するヘッドホンを先に接続してください。"},
            {"tray_battery", "Sony Device Center · バッテリー %1"},
            {"tray_battery_unknown", "Sony Device Center · バッテリー不明"},
            {"tray_disconnected", "Sony Device Center · 未接続"}
        }},
    };
    for (auto localeIt = additionalTranslations.cbegin();
         localeIt != additionalTranslations.cend(); ++localeIt) {
        const auto& locale = localeIt.key();
        const auto& values = localeIt.value();
        for (auto valueIt = values.cbegin(); valueIt != values.cend(); ++valueIt)
            _strings[locale][valueIt.key()] = valueIt.value();
    }
}
} // namespace sony::devicecenter
