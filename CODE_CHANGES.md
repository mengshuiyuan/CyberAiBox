# 代码改动说明

本文件记录了为实现双屏板使用left目录表情而进行的所有代码改动。

## 1. CMakeLists.txt

**文件路径**：`/esp-project/binocular-display-of-lily-rabbit/main/CMakeLists.txt`

### 改动1：修复条件语句（行68）

**旧代码**：
```cmake
elseif(CONFIG_BOARD_TYPE_DUAL_SCREEN_CYBERAI)
```

**新代码**：
```cmake
if(CONFIG_BOARD_TYPE_DUAL_SCREEN_CYBERAI)
```

**改动原因**：修复了CMake条件语句嵌套错误，将`elseif`改为`if`，解决了构建失败的问题。

### 改动2：添加表情集合配置（行72）

**旧代码**：
```cmake
    set(BOARD_TYPE "dual-screen")
    set(BUILTIN_TEXT_FONT font_puhui_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
```

**新代码**：
```cmake
    set(BOARD_TYPE "dual-screen")
    set(BUILTIN_TEXT_FONT font_puhui_16_4)
    set(BUILTIN_ICON_FONT font_awesome_16_4)
    set(DEFAULT_EMOJI_COLLECTION twemoji_64)
```

**改动原因**：为双屏板添加了表情集合配置，确保build_default_assets.py脚本能够正确处理表情。

## 2. assets.cc

**文件路径**：`/esp-project/binocular-display-of-lily-rabbit/main/assets.cc`

### 改动：增强表情加载逻辑（行170-243）

**旧代码**：b
```cpp
    cJSON* emoji_collection = cJSON_GetObjectItem(root, "emoji_collection");
    if (cJSON_IsArray(emoji_collection)) {
        auto custom_emoji_collection = std::make_shared<EmojiCollection>();
        int emoji_count = cJSON_GetArraySize(emoji_collection);
        for (int i = 0; i < emoji_count; i++) {
            cJSON* emoji = cJSON_GetArrayItem(emoji_collection, i);
            if (cJSON_IsObject(emoji)) {
                cJSON* name = cJSON_GetObjectItem(emoji, "name");
                cJSON* file = cJSON_GetObjectItem(emoji, "file");
                cJSON* eaf = cJSON_GetObjectItem(emoji, "eaf");
                if (cJSON_IsString(name) && cJSON_IsString(file) && (NULL== eaf)) {
                    if (!GetAssetData(file->valuestring, ptr, size)) {
                        // Try to load from left directory
                        std::string left_path = "/esp-project/binocular-display-of-lily-rabbit/left/";
                        left_path += name->valuestring;
                        left_path += ".gif";
                        
                        FILE* left_file = fopen(left_path.c_str(), "rb");
                        if (left_file != NULL) {
                            // Get file size
                            fseek(left_file, 0, SEEK_END);
                            size = ftell(left_file);
                            fseek(left_file, 0, SEEK_SET);
                            
                            // Allocate buffer and read file
                            ptr = malloc(size);
                            if (ptr != NULL) {
                                fread(ptr, 1, size, left_file);
                                ESP_LOGI(TAG, "Loaded emoji %s from left directory", name->valuestring);
                            } else {
                                ESP_LOGE(TAG, "Failed to allocate memory for emoji %s from left directory", name->valuestring);
                                fclose(left_file);
                                continue;
                            }
                            fclose(left_file);
                        } else {
                            ESP_LOGE(TAG, "Emoji %s image file %s is not found", name->valuestring, file->valuestring);
                            continue;
                        }
                    }
                    custom_emoji_collection->AddEmoji(name->valuestring, new LvglRawImage(ptr, size));
                }
            }
        }
        if (light_theme != nullptr) {
            light_theme->set_emoji_collection(custom_emoji_collection);
        }
        if (dark_theme != nullptr) {
            dark_theme->set_emoji_collection(custom_emoji_collection);
        }
    }
```

**新代码**：
```cpp
    // Always create a custom emoji collection
    auto custom_emoji_collection = std::make_shared<EmojiCollection>();
    
    // List of emoji names to load from left directory
    const char* emoji_names[] = {
        "neutral", "happy", "laughing", "funny", "sad", "angry", "crying", 
        "loving", "embarrassed", "surprised", "shocked", "thinking", "winking", 
        "cool", "relaxed", "delicious", "kissy", "confident", "sleepy", "silly", "confused"
    };
    
    // Try to load emojis from left directory first
    bool loaded_from_left = false;
    for (const char* name : emoji_names) {
        // Try to find emoji in index.json first
        bool found_in_index = false;
        cJSON* emoji_collection = cJSON_GetObjectItem(root, "emoji_collection");
        if (cJSON_IsArray(emoji_collection)) {
            int emoji_count = cJSON_GetArraySize(emoji_collection);
            for (int i = 0; i < emoji_count; i++) {
                cJSON* emoji = cJSON_GetArrayItem(emoji_collection, i);
                if (cJSON_IsObject(emoji)) {
                    cJSON* emoji_name = cJSON_GetObjectItem(emoji, "name");
                    cJSON* emoji_file = cJSON_GetObjectItem(emoji, "file");
                    cJSON* emoji_eaf = cJSON_GetObjectItem(emoji, "eaf");
                    if (cJSON_IsString(emoji_name) && strcmp(emoji_name->valuestring, name) == 0 && 
                        cJSON_IsString(emoji_file) && (NULL == emoji_eaf)) {
                        if (GetAssetData(emoji_file->valuestring, ptr, size)) {
                            custom_emoji_collection->AddEmoji(name, new LvglRawImage(ptr, size));
                            found_in_index = true;
                            break;
                        }
                    }
                }
            }
        }
        
        // If not found in index.json, try to load from left directory
        if (!found_in_index) {
            std::string left_path = "/esp-project/binocular-display-of-lily-rabbit/left/";
            left_path += name;
            left_path += ".gif";
            
            FILE* left_file = fopen(left_path.c_str(), "rb");
            if (left_file != NULL) {
                // Get file size
                fseek(left_file, 0, SEEK_END);
                size = ftell(left_file);
                fseek(left_file, 0, SEEK_SET);
                
                // Allocate buffer and read file
                ptr = malloc(size);
                if (ptr != NULL) {
                    fread(ptr, 1, size, left_file);
                    custom_emoji_collection->AddEmoji(name, new LvglRawImage(ptr, size));
                    ESP_LOGI(TAG, "Loaded emoji %s from left directory", name);
                    loaded_from_left = true;
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for emoji %s from left directory", name);
                }
                fclose(left_file);
            }
        }
    }
    
    // Set custom emoji collection to themes
    if (light_theme != nullptr) {
        light_theme->set_emoji_collection(custom_emoji_collection);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_emoji_collection(custom_emoji_collection);
    }
    
    ESP_LOGI(TAG, "Emoji loading complete, loaded_from_left: %s", loaded_from_left ? "true" : "false");
```

**改动原因**：增强了表情加载逻辑，确保即使index.json中没有emoji_collection数组，也能从left目录加载表情。

## 3. emoji_collection.cc

**文件路径**：`/esp-project/binocular-display-of-lily-rabbit/main/display/lvgl_display/emoji_collection.cc`

### 改动：简化表情集合构造函数（行101-126）

**旧代码**：
```cpp
Twemoji64::Twemoji64() {
    // Try to load from left directory first
    bool loaded_from_left = false;
    
    // List of emoji names
    const char* emoji_names[] = {
        "neutral", "happy", "laughing", "funny", "sad", "angry", "crying", 
        "loving", "embarrassed", "surprised", "shocked", "thinking", "winking", 
        "cool", "relaxed", "delicious", "kissy", "confident", "sleepy", "silly", "confused"
    };
    
    ESP_LOGI(TAG, "Twemoji64 constructor: trying to load from left directory");
    
    // Try to load from left directory - use relative path that works on device
    for (const char* name : emoji_names) {
        // Try multiple possible paths
        std::string left_paths[] = {
            "/left/" + std::string(name) + ".gif",
            "left/" + std::string(name) + ".gif",
            "/esp-project/binocular-display-of-lily-rabbit/left/" + std::string(name) + ".gif"
        };
        
        FILE* file = NULL;
        std::string used_path;
        
        // Try each path until we find a valid one
        for (const auto& path : left_paths) {
            file = fopen(path.c_str(), "rb");
            if (file != NULL) {
                used_path = path;
                ESP_LOGI(TAG, "Found emoji %s at path: %s", name, used_path.c_str());
                break;
            }
        }
        
        if (file != NULL) {
            // Get file size
            fseek(file, 0, SEEK_END);
            size_t file_size = ftell(file);
            fseek(file, 0, SEEK_SET);
            ESP_LOGI(TAG, "Emoji %s file size: %d bytes", name, file_size);
            
            // Allocate buffer and read file
            void* buffer = malloc(file_size);
            if (buffer != NULL) {
                fread(buffer, 1, file_size, file);
                // Add emoji from left directory
                AddEmoji(name, new LvglRawImage(buffer, file_size));
                ESP_LOGI(TAG, "Successfully loaded emoji %s from left directory", name);
                loaded_from_left = true;
            } else {
                ESP_LOGE(TAG, "Failed to allocate memory for emoji %s", name);
            }
            fclose(file);
        } else {
            ESP_LOGW(TAG, "Could not find emoji %s in left directory", name);
        }
    }
    
    ESP_LOGI(TAG, "Left directory emoji loading complete, loaded_from_left: %s", loaded_from_left ? "true" : "false");
    
    // If no emojis loaded from left directory, use default ones
    if (!loaded_from_left) {
        ESP_LOGI(TAG, "Falling back to default emojis");
        AddEmoji("neutral", new LvglSourceImage(&emoji_1f636_64));
        AddEmoji("happy", new LvglSourceImage(&emoji_1f642_64));
        AddEmoji("laughing", new LvglSourceImage(&emoji_1f606_64));
        AddEmoji("funny", new LvglSourceImage(&emoji_1f602_64));
        AddEmoji("sad", new LvglSourceImage(&emoji_1f614_64));
        AddEmoji("angry", new LvglSourceImage(&emoji_1f620_64));
        AddEmoji("crying", new LvglSourceImage(&emoji_1f62d_64));
        AddEmoji("loving", new LvglSourceImage(&emoji_1f60d_64));
        AddEmoji("embarrassed", new LvglSourceImage(&emoji_1f633_64));
        AddEmoji("surprised", new LvglSourceImage(&emoji_1f62f_64));
        AddEmoji("shocked", new LvglSourceImage(&emoji_1f631_64));
        AddEmoji("thinking", new LvglSourceImage(&emoji_1f914_64));
        AddEmoji("winking", new LvglSourceImage(&emoji_1f609_64));
        AddEmoji("cool", new LvglSourceImage(&emoji_1f60e_64));
        AddEmoji("relaxed", new LvglSourceImage(&emoji_1f60c_64));
        AddEmoji("delicious", new LvglSourceImage(&emoji_1f924_64));
        AddEmoji("kissy", new LvglSourceImage(&emoji_1f618_64));
        AddEmoji("confident", new LvglSourceImage(&emoji_1f60f_64));
        AddEmoji("sleepy", new LvglSourceImage(&emoji_1f634_64));
        AddEmoji("silly", new LvglSourceImage(&emoji_1f61c_64));
        AddEmoji("confused", new LvglSourceImage(&emoji_1f644_64));
    }
}
```

**新代码**：
```cpp
Twemoji64::Twemoji64() {
    // Use default emojis as fallback
    // These will be replaced by custom ones from assets.cc if available
    ESP_LOGI(TAG, "Twemoji64 constructor: using default emojis");
    AddEmoji("neutral", new LvglSourceImage(&emoji_1f636_64));
    AddEmoji("happy", new LvglSourceImage(&emoji_1f642_64));
    AddEmoji("laughing", new LvglSourceImage(&emoji_1f606_64));
    AddEmoji("funny", new LvglSourceImage(&emoji_1f602_64));
    AddEmoji("sad", new LvglSourceImage(&emoji_1f614_64));
    AddEmoji("angry", new LvglSourceImage(&emoji_1f620_64));
    AddEmoji("crying", new LvglSourceImage(&emoji_1f62d_64));
    AddEmoji("loving", new LvglSourceImage(&emoji_1f60d_64));
    AddEmoji("embarrassed", new LvglSourceImage(&emoji_1f633_64));
    AddEmoji("surprised", new LvglSourceImage(&emoji_1f62f_64));
    AddEmoji("shocked", new LvglSourceImage(&emoji_1f631_64));
    AddEmoji("thinking", new LvglSourceImage(&emoji_1f914_64));
    AddEmoji("winking", new LvglSourceImage(&emoji_1f609_64));
    AddEmoji("cool", new LvglSourceImage(&emoji_1f60e_64));
    AddEmoji("relaxed", new LvglSourceImage(&emoji_1f60c_64));
    AddEmoji("delicious", new LvglSourceImage(&emoji_1f924_64));
    AddEmoji("kissy", new LvglSourceImage(&emoji_1f618_64));
    AddEmoji("confident", new LvglSourceImage(&emoji_1f60f_64));
    AddEmoji("sleepy", new LvglSourceImage(&emoji_1f634_64));
    AddEmoji("silly", new LvglSourceImage(&emoji_1f61c_64));
    AddEmoji("confused", new LvglSourceImage(&emoji_1f644_64));
}
```

**改动原因**：简化了Twemoji64构造函数，移除了直接访问文件系统的代码，改为使用assets.cc提供的表情集合，实现了更好的模块化设计。

## 4. build_default_assets.py

**文件路径**：`/esp-project/binocular-display-of-lily-rabbit/scripts/build_default_assets.py`

### 改动：增强表情处理逻辑（行218-270）

**旧代码**：
```python
def process_emoji_collection(emoji_collection_dir, assets_dir):
    """Process emoji_collection parameter"""
    emoji_list = []
    
    if emoji_collection_dir:
        print(f"Using provided emoji collection: {emoji_collection_dir}")
        for root, dirs, files in os.walk(emoji_collection_dir):
            for file in files:
                if file.lower().endswith(('.png', '.gif')):
                    # Copy file
                    src_file = os.path.join(root, file)
                    dst_file = os.path.join(assets_dir, file)
                    if copy_file(src_file, dst_file):
                        # Get filename without extension
                        filename_without_ext = os.path.splitext(file)[0]
                        
                        # Add to emoji list
                        emoji_list.append({
                            "name": filename_without_ext,
                            "file": file
                        })
    
    if emoji_list:
        print(f"Added {len(emoji_list)} emojis from provided collection")
    else:
        print("No emojis found in provided collection")
    
    return emoji_list
```

**新代码**：
```python
def process_emoji_collection(emoji_collection_dir, assets_dir):
    """Process emoji_collection parameter"""
    emoji_list = []
    
    # First try to use left directory if it exists
    left_dir = "/esp-project/binocular-display-of-lily-rabbit/left"
    if os.path.exists(left_dir):
        print(f"Using left directory for emojis: {left_dir}")
        for root, dirs, files in os.walk(left_dir):
            for file in files:
                if file.lower().endswith('.gif'):
                    # Copy file
                    src_file = os.path.join(root, file)
                    dst_file = os.path.join(assets_dir, file)
                    if copy_file(src_file, dst_file):
                        # Get filename without extension
                        filename_without_ext = os.path.splitext(file)[0]
                        
                        # Add to emoji list
                        emoji_list.append({
                            "name": filename_without_ext,
                            "file": file
                        })
        
        if emoji_list:
            print(f"Added {len(emoji_list)} emojis from left directory")
            return emoji_list
    
    # If left directory doesn't exist or has no emojis, use the provided emoji_collection_dir
    if emoji_collection_dir:
        print(f"Using provided emoji collection: {emoji_collection_dir}")
        for root, dirs, files in os.walk(emoji_collection_dir):
            for file in files:
                if file.lower().endswith(('.png', '.gif')):
                    # Copy file
                    src_file = os.path.join(root, file)
                    dst_file = os.path.join(assets_dir, file)
                    if copy_file(src_file, dst_file):
                        # Get filename without extension
                        filename_without_ext = os.path.splitext(file)[0]
                        
                        # Add to emoji list
                        emoji_list.append({
                            "name": filename_without_ext,
                            "file": file
                        })
    
    if emoji_list:
        print(f"Added {len(emoji_list)} emojis from provided collection")
    else:
        print("No emojis found in either left directory or provided collection")
    
    return emoji_list
```

**改动原因**：增强了表情处理逻辑，优先使用left目录中的表情，只有当left目录不存在或没有表情时，才使用提供的表情集合，确保left目录中的表情能够被正确处理。

## 5. backlight.cc

**文件路径**：`/esp-project/binocular-display-of-lily-rabbit/main/boards/common/backlight.cc`

### 改动：提高默认背光亮度（行32-44）

**旧代码**：
```cpp
void Backlight::RestoreBrightness() {
    // Load brightness from settings
    Settings settings("display");  
    int saved_brightness = settings.GetInt("brightness", 75);
    
    // 检查亮度值是否为0或过小，设置默认值
    if (saved_brightness <= 0) {
        ESP_LOGW(TAG, "Brightness value (%d) is too small, setting to default (10)", saved_brightness);
        saved_brightness = 10;  // 设置一个较低的默认值
    }
    
    SetBrightness(saved_brightness);
}
```

**新代码**：
```cpp
void Backlight::RestoreBrightness() {
    // Load brightness from settings
    Settings settings("display");  
    int saved_brightness = settings.GetInt("brightness", 90);
    
    // 检查亮度值是否为0或过小，设置默认值
    if (saved_brightness <= 0) {
        ESP_LOGW(TAG, "Brightness value (%d) is too small, setting to default (90)", saved_brightness);
        saved_brightness = 90;  // 设置一个较高的默认值
    }
    
    SetBrightness(saved_brightness);
}
```

**改动原因**：提高了默认背光亮度，从75增加到90，使屏幕更加明亮。

## 6. dual-screen.cc

**文件路径**：`/esp-project/binocular-display-of-lily-rabbit/main/boards/dual-screen/dual-screen.cc`

### 改动：添加右侧屏幕背光初始化（行561-571）

**旧代码**：
```cpp
        if (DISPLAY_LEFT_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
```

**新代码**：
```cpp
        // Initialize left backlight
        if (DISPLAY_LEFT_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        
        // Initialize right backlight if available
        if (DISPLAY_RIGHT_BACKLIGHT_PIN != GPIO_NUM_NC) {
            // Create a temporary backlight instance for the right screen
            static PwmBacklight right_backlight(DISPLAY_RIGHT_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            right_backlight.RestoreBrightness();
        }
```

**改动原因**：添加了右侧屏幕背光的初始化代码，确保两侧屏幕的背光都能被正确调整到合适的亮度。

## 总结

以上就是为实现双屏板使用left目录表情和提高屏幕亮度而进行的所有代码改动。这些改动确保了：

1. 双屏板能够正确识别left目录中的表情文件
2. 表情文件能够被正确打包到assets.bin中
3. 设备能够从assets.bin中加载并显示left目录中的表情
4. 屏幕背光亮度提高，从默认的75增加到90
5. 两侧屏幕的背光都能被正确初始化和调整
6. 代码结构更加模块化，便于维护和扩展

这些改动共同实现了双屏板使用left目录表情和提高屏幕亮度的功能，提高了设备的灵活性、可定制性和用户体验。