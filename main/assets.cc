#include "assets.h"
#include "board.h"
#include "display.h"
#include "application.h"
#include "lvgl_theme.h"
#include "emote_display.h"

#include <esp_log.h>
#include <spi_flash_mmap.h>
#include <esp_timer.h>
#include <cbin_font.h>


#define TAG "Assets"

struct mmap_assets_table {
    char asset_name[32];          /*!< Name of the asset */
    uint32_t asset_size;          /*!< Size of the asset */
    uint32_t asset_offset;        /*!< Offset of the asset */
    uint16_t asset_width;         /*!< Width of the asset */
    uint16_t asset_height;        /*!< Height of the asset */
};

// 表情映射表 - 根据提供的映射关系定义
struct EmoteMapping {
    const char* emote_name;      // 表情名称
    const char* gif_filename;    // 对应的GIF文件名
};

// 表情映射表定义
static const EmoteMapping emote_mappings[] = {
    {"happy",       "happy.gif"},
    {"laughing",    "happy.gif"},
    {"funny",       "happy.gif"},
    {"loving",      "loving.gif"},
    {"embarrassed", "happy.gif"},
    {"confident",   "happy.gif"},
    {"delicious",   "delicious.gif"},
    {"sad",         "sad.gif"},
    {"crying",      "crying.gif"},
    {"sleepy",      "sleepy.gif"},
    {"silly",       "happy.gif"},
    {"angry",       "angry.gif"},
    {"surprised",   "confused.gif"},
    {"shocked",     "shocked.gif"},
    {"thinking",    "thinking.gif"},
    {"winking",     "winking.gif"},
    {"relaxed",     "happy.gif"},
    {"confused",    "confused.gif"},
    {"neutral",     "sleepy.gif"},  
    {"kissy",       "loving.gif"},
    {"cool",         "happy.gif"}
};

static const int total_emote_mappings = sizeof(emote_mappings) / sizeof(emote_mappings[0]);

Assets::Assets() {
    // Initialize the partition
    InitializePartition();
}

Assets::~Assets() {
    if (mmap_handle_ != 0) {
        esp_partition_munmap(mmap_handle_);
    }
}

uint32_t Assets::CalculateChecksum(const char* data, uint32_t length) {
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < length; i++) {
        checksum += data[i];
    }
    return checksum & 0xFFFF;
}

bool Assets::InitializePartition() {
    partition_valid_ = false;
    checksum_valid_ = false;
    assets_.clear();

    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "assets");
    if (partition_ == nullptr) {
        ESP_LOGI(TAG, "No assets partition found");
        return false;
    }

    int free_pages = spi_flash_mmap_get_free_pages(SPI_FLASH_MMAP_DATA);
    uint32_t storage_size = free_pages * 64 * 1024;
    ESP_LOGI(TAG, "The storage free size is %ld KB", storage_size / 1024);
    ESP_LOGI(TAG, "The partition size is %ld KB", partition_->size / 1024);
    if (storage_size < partition_->size) {
        ESP_LOGE(TAG, "The free size %ld KB is less than assets partition required %ld KB", storage_size / 1024, partition_->size / 1024);
        return false;
    }

    esp_err_t err = esp_partition_mmap(partition_, 0, partition_->size, ESP_PARTITION_MMAP_DATA, (const void**)&mmap_root_, &mmap_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap assets partition: %s", esp_err_to_name(err));
        return false;
    }

    partition_valid_ = true;

    uint32_t stored_files = *(uint32_t*)(mmap_root_ + 0);
    uint32_t stored_chksum = *(uint32_t*)(mmap_root_ + 4);
    uint32_t stored_len = *(uint32_t*)(mmap_root_ + 8);

    if (stored_len > partition_->size - 12) {
        ESP_LOGD(TAG, "The stored_len (0x%lx) is greater than the partition size (0x%lx) - 12", stored_len, partition_->size);
        return false;
    }

    auto start_time = esp_timer_get_time();
    uint32_t calculated_checksum = CalculateChecksum(mmap_root_ + 12, stored_len);
    auto end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "The checksum calculation time is %d ms", int((end_time - start_time) / 1000));

    if (calculated_checksum != stored_chksum) {
        ESP_LOGE(TAG, "The calculated checksum (0x%lx) does not match the stored checksum (0x%lx)", calculated_checksum, stored_chksum);
        return false;
    }

    checksum_valid_ = true;

    for (uint32_t i = 0; i < stored_files; i++) {
        auto item = (const mmap_assets_table*)(mmap_root_ + 12 + i * sizeof(mmap_assets_table));
        auto asset = Asset{
            .size = static_cast<size_t>(item->asset_size),
            .offset = static_cast<size_t>(12 + sizeof(mmap_assets_table) * stored_files + item->asset_offset)
        };
        assets_[item->asset_name] = asset;
    }
    return checksum_valid_;
}

bool Assets::Apply() {
    void* ptr = nullptr;
    size_t size = 0;
    if (!GetAssetData("index.json", ptr, size)) {
        ESP_LOGE(TAG, "The index.json file is not found");
        return false;
    }

    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
    if (root == nullptr) {
        ESP_LOGE(TAG, "The index.json file is not valid");
        return false;
    }

    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (cJSON_IsNumber(version)) {
        if (version->valuedouble > 1) {
            ESP_LOGE(TAG, "The assets version %d is not supported, please upgrade the firmware", version->valueint);
            return false;
        }
    }
    
    cJSON* srmodels = cJSON_GetObjectItem(root, "srmodels");
    if (cJSON_IsString(srmodels)) {
        std::string srmodels_file = srmodels->valuestring;
        if (GetAssetData(srmodels_file, ptr, size)) {
            if (models_list_ != nullptr) {
                esp_srmodel_deinit(models_list_);
                models_list_ = nullptr;
            }
            models_list_ = srmodel_load(static_cast<uint8_t*>(ptr));
            if (models_list_ != nullptr) {
                auto& app = Application::GetInstance();
                app.GetAudioService().SetModelsList(models_list_);
            } else {
                ESP_LOGE(TAG, "Failed to load srmodels.bin");
            }
        } else {
            ESP_LOGE(TAG, "The srmodels file %s is not found", srmodels_file.c_str());
        }
    }

#ifdef HAVE_LVGL
    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");

    cJSON* font = cJSON_GetObjectItem(root, "text_font");
    if (cJSON_IsString(font)) {
        std::string fonts_text_file = font->valuestring;
        if (GetAssetData(fonts_text_file, ptr, size)) {
            auto text_font = std::make_shared<LvglCBinFont>(ptr);
            if (text_font->font() == nullptr) {
                ESP_LOGE(TAG, "Failed to load fonts.bin");
                return false;
            }
            if (light_theme != nullptr) {
                light_theme->set_text_font(text_font);
            }
            if (dark_theme != nullptr) {
                dark_theme->set_text_font(text_font);
            }
        } else {
            ESP_LOGE(TAG, "The font file %s is not found", fonts_text_file.c_str());
        }
    }

    // Always create a custom emoji collection
    auto custom_emoji_collection = std::make_shared<EmojiCollection>();
    
    // List of emoji names to load from left directory
    const char* emoji_names[] = {
        "neutral", "happy", "laughing", "funny", "sad", "angry", "crying", 
        "loving", "embarrassed", "surprised", "shocked", "thinking", "winking", 
        "cool", "relaxed", "delicious", "kissy", "confident", "sleepy", "silly", "confused"
    };
    
    const int total_emojis = sizeof(emoji_names) / sizeof(emoji_names[0]);
    ESP_LOGI(TAG, "开始加载表情包，共 %d 个表情", total_emojis);
    
    // 统计信息
    int loaded_from_index = 0;
    int loaded_from_left = 0;
    int loaded_from_assets = 0;
    int failed_to_load = 0;
    int skipped_from_eaf = 0;
    
    // 按照映射表加载表情
    for (int idx = 0; idx < total_emote_mappings; idx++) {
        const EmoteMapping& mapping = emote_mappings[idx];
        const char* emote_name = mapping.emote_name;
        const char* gif_filename = mapping.gif_filename;
        
        ESP_LOGI(TAG, "正在加载表情 [%d/%d]: %s -> %s", 
                idx + 1, total_emote_mappings, emote_name, gif_filename);
        
        // 尝试从index.json中查找表情（支持自定义映射）
        bool found_in_index = false;
        cJSON* emoji_collection = cJSON_GetObjectItem(root, "emoji_collection");
        
        if (emoji_collection && cJSON_IsArray(emoji_collection)) {
            int emoji_count = cJSON_GetArraySize(emoji_collection);
            
            for (int i = 0; i < emoji_count; i++) {
                cJSON* emoji = cJSON_GetArrayItem(emoji_collection, i);
                if (!cJSON_IsObject(emoji)) continue;
                
                cJSON* emoji_name = cJSON_GetObjectItem(emoji, "name");
                cJSON* emoji_file = cJSON_GetObjectItem(emoji, "file");
                cJSON* emoji_eaf = cJSON_GetObjectItem(emoji, "eaf");
                
                if (emoji_name == NULL || emoji_file == NULL) continue;
                
                // 检查是否有自定义映射或使用默认映射
                if (strcmp(emoji_name->valuestring, emote_name) == 0) {
                    ESP_LOGI(TAG, "在 index.json 中找到表情 %s", emote_name);
                    
                    if (emoji_eaf != NULL) {
                        ESP_LOGI(TAG, "表情 %s 有 eaf 字段，跳过加载", emote_name);
                        skipped_from_eaf++;
                        found_in_index = true;
                        break;
                    }
                    
                    // 使用index.json中指定的文件名
                    const char* actual_filename = emoji_file->valuestring;
                    ESP_LOGI(TAG, "尝试从 assets 加载表情 %s: %s", emote_name, actual_filename);
                    
                    if (GetAssetData(actual_filename, ptr, size)) {
                        ESP_LOGI(TAG, "成功从 assets 加载表情 %s，文件大小: %ld 字节", emote_name, size);
                        custom_emoji_collection->AddEmoji(emote_name, new LvglRawImage(ptr, size));
                        loaded_from_index++;
                        found_in_index = true;
                    } else {
                        ESP_LOGE(TAG, "从 assets 加载表情 %s 失败，文件: %s", emote_name, actual_filename);
                    }
                    break;
                }
            }
        }
        
        // 如果未在index.json中找到，使用映射表中的GIF文件名
        if (!found_in_index) {
            // 首先尝试从assets中加载
            if (GetAssetData(gif_filename, ptr, size)) {
                ESP_LOGI(TAG, "成功从 assets 加载映射表情 %s -> %s，大小: %ld 字节", 
                        emote_name, gif_filename, size);
                custom_emoji_collection->AddEmoji(emote_name, new LvglRawImage(ptr, size));
                loaded_from_assets++;
                continue;
            }
            
            // 如果assets中没有，尝试从左目录加载
            std::string left_path = "./../left/";
            left_path += gif_filename;
            
            ESP_LOGI(TAG, "尝试从左目录加载表情 %s: %s", emote_name, left_path.c_str());
            
            FILE* left_file = fopen(left_path.c_str(), "rb");
            if (left_file == NULL) {
                ESP_LOGE(TAG, "无法打开文件 %s, errno: %d", left_path.c_str(), errno);
                failed_to_load++;
                continue;
            }
            
            // 获取文件大小
            if (fseek(left_file, 0, SEEK_END) != 0) {
                ESP_LOGE(TAG, "fseek(SEEK_END) 失败, errno: %d", errno);
                fclose(left_file);
                failed_to_load++;
                continue;
            }
            
            long file_size = ftell(left_file);
            if (file_size < 0) {
                ESP_LOGE(TAG, "ftell 失败, errno: %d", errno);
                fclose(left_file);
                failed_to_load++;
                continue;
            }
            
            if (fseek(left_file, 0, SEEK_SET) != 0) {
                ESP_LOGE(TAG, "fseek(SEEK_SET) 失败, errno: %d", errno);
                fclose(left_file);
                failed_to_load++;
                continue;
            }
            
            ESP_LOGI(TAG, "文件 %s 大小: %ld 字节", left_path.c_str(), file_size);
            
            // 分配缓冲区并读取文件
            ptr = malloc(file_size);
            if (ptr == NULL) {
                ESP_LOGE(TAG, "内存分配失败，需要 %ld 字节", file_size);
                fclose(left_file);
                failed_to_load++;
                continue;
            }
            
            size_t bytes_read = fread(ptr, 1, file_size, left_file);
            if (bytes_read != file_size) {
                ESP_LOGE(TAG, "文件读取不完整: 读取 %zu 字节，期望 %ld 字节", bytes_read, file_size);
                free(ptr);
                fclose(left_file);
                failed_to_load++;
                continue;
            }
            
            if (ferror(left_file)) {
                ESP_LOGE(TAG, "文件读取错误");
                free(ptr);
                fclose(left_file);
                failed_to_load++;
                continue;
            }
            
            fclose(left_file);
            
            // 验证图像数据
            if (ptr != NULL && file_size > 0) {
                ESP_LOGI(TAG, "成功从左目录加载表情 %s，大小: %ld 字节", emote_name, file_size);
                custom_emoji_collection->AddEmoji(emote_name, new LvglRawImage(ptr, file_size));
                loaded_from_left++;
                free(ptr); // 注意：LvglRawImage会复制数据，所以这里可以释放
            } else {
                ESP_LOGE(TAG, "无效的表情数据: ptr=%p, size=%ld", ptr, file_size);
                if (ptr != NULL) free(ptr);
                failed_to_load++;
            }
        }
    }
    
    // 总结加载结果
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "表情加载完成:");
    ESP_LOGI(TAG, "  从 index.json 自定义加载: %d", loaded_from_index);
    ESP_LOGI(TAG, "  从 assets 映射加载: %d", loaded_from_assets);
    ESP_LOGI(TAG, "  从左目录加载: %d", loaded_from_left);
    ESP_LOGI(TAG, "  跳过(eaf): %d", skipped_from_eaf);
    ESP_LOGI(TAG, "  加载失败: %d", failed_to_load);
    ESP_LOGI(TAG, "  总共成功加载: %d", loaded_from_index + loaded_from_assets + loaded_from_left);
    ESP_LOGI(TAG, "==========================================");
    
    // 检查是否有表情成功加载
    if ((loaded_from_index + loaded_from_assets + loaded_from_left) == 0) {
        ESP_LOGW(TAG, "警告: 没有加载到任何表情!");
    } else if (failed_to_load > 0) {
        ESP_LOGW(TAG, "警告: %d 个表情加载失败", failed_to_load);
    }
    
    // Set custom emoji collection to themes
    if (light_theme != nullptr) {
        light_theme->set_emoji_collection(custom_emoji_collection);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_emoji_collection(custom_emoji_collection);
    }
    
    ESP_LOGI(TAG, "Emoji loading complete");

    cJSON* skin = cJSON_GetObjectItem(root, "skin");
    if (cJSON_IsObject(skin)) {
        cJSON* light_skin = cJSON_GetObjectItem(skin, "light");
        if (cJSON_IsObject(light_skin) && light_theme != nullptr) {
            cJSON* text_color = cJSON_GetObjectItem(light_skin, "text_color");
            cJSON* background_color = cJSON_GetObjectItem(light_skin, "background_color");
            cJSON* background_image = cJSON_GetObjectItem(light_skin, "background_image");
            if (cJSON_IsString(text_color)) {
                light_theme->set_text_color(LvglTheme::ParseColor(text_color->valuestring));
            }
            if (cJSON_IsString(background_color)) {
                light_theme->set_background_color(LvglTheme::ParseColor(background_color->valuestring));
                light_theme->set_chat_background_color(LvglTheme::ParseColor(background_color->valuestring));
            }
            if (cJSON_IsString(background_image)) {
                if (!GetAssetData(background_image->valuestring, ptr, size)) {
                    ESP_LOGE(TAG, "The background image file %s is not found", background_image->valuestring);
                    return false;
                }
                auto background_image = std::make_shared<LvglCBinImage>(ptr);
                light_theme->set_background_image(background_image);
            }
        }
        cJSON* dark_skin = cJSON_GetObjectItem(skin, "dark");
        if (cJSON_IsObject(dark_skin) && dark_theme != nullptr) {
            cJSON* text_color = cJSON_GetObjectItem(dark_skin, "text_color");
            cJSON* background_color = cJSON_GetObjectItem(dark_skin, "background_color");
            cJSON* background_image = cJSON_GetObjectItem(dark_skin, "background_image");
            if (cJSON_IsString(text_color)) {
                dark_theme->set_text_color(LvglTheme::ParseColor(text_color->valuestring));
            }
            if (cJSON_IsString(background_color)) {
                dark_theme->set_background_color(LvglTheme::ParseColor(background_color->valuestring));
                dark_theme->set_chat_background_color(LvglTheme::ParseColor(background_color->valuestring));
            }
            if (cJSON_IsString(background_image)) {
                if (!GetAssetData(background_image->valuestring, ptr, size)) {
                    ESP_LOGE(TAG, "The background image file %s is not found", background_image->valuestring);
                    return false;
                }
                auto background_image = std::make_shared<LvglCBinImage>(ptr);
                dark_theme->set_background_image(background_image);
            }
        }
    }

    auto display = Board::GetInstance().GetDisplay();
    ESP_LOGI(TAG, "Refreshing display theme...");

    auto current_theme = display->GetTheme();
    if (current_theme != nullptr) {
        display->SetTheme(current_theme);
    }
#elif defined(CONFIG_USE_EMOTE_MESSAGE_STYLE)
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto emote_display = dynamic_cast<emote::EmoteDisplay*>(display);

    cJSON* font = cJSON_GetObjectItem(root, "text_font");
    if (cJSON_IsString(font)) {
        std::string fonts_text_file = font->valuestring;
        if (GetAssetData(fonts_text_file, ptr, size)) {
            auto text_font = std::make_shared<LvglCBinFont>(ptr);
            if (text_font->font() == nullptr) {
                ESP_LOGE(TAG, "Failed to load fonts.bin");
                return false;
            }

            if (emote_display) {
                emote_display->AddTextFont(text_font);
            }
        } else {
            ESP_LOGE(TAG, "The font file %s is not found", fonts_text_file.c_str());
        }
    }

    cJSON* emoji_collection = cJSON_GetObjectItem(root, "emoji_collection");
    if (cJSON_IsArray(emoji_collection)) {
        int emoji_count = cJSON_GetArraySize(emoji_collection);
        if (emote_display) {
            for (int i = 0; i < emoji_count; i++) {
                cJSON* icon = cJSON_GetArrayItem(emoji_collection, i);
                if (cJSON_IsObject(icon)) {
                    cJSON* name = cJSON_GetObjectItem(icon, "name");
                    cJSON* file = cJSON_GetObjectItem(icon, "file");

                    if (cJSON_IsString(name) && cJSON_IsString(file)) {
                        if (GetAssetData(file->valuestring, ptr, size)) {
                            cJSON* eaf = cJSON_GetObjectItem(icon, "eaf");
                            bool lack_value = false;
                            bool loop_value = false;
                            int fps_value = 0;

                            if (cJSON_IsObject(eaf)) {
                                cJSON* lack = cJSON_GetObjectItem(eaf, "lack");
                                cJSON* loop = cJSON_GetObjectItem(eaf, "loop");
                                cJSON* fps = cJSON_GetObjectItem(eaf, "fps");

                                lack_value = lack ? cJSON_IsTrue(lack) : false;
                                loop_value = loop ? cJSON_IsTrue(loop) : false;
                                fps_value = fps ? fps->valueint : 0;

                                emote_display->AddEmojiData(name->valuestring, ptr, size,
                                                          static_cast<uint8_t>(fps_value),
                                                          loop_value, lack_value);
                            }

                        } else {
                            ESP_LOGE(TAG, "Emoji \"%10s\" image file %s is not found", name->valuestring, file->valuestring);
                        }
                    }
                }
            }
        }
    }

    cJSON* icon_collection = cJSON_GetObjectItem(root, "icon_collection");
    if (cJSON_IsArray(icon_collection)) {
        if (emote_display) {
            int icon_count = cJSON_GetArraySize(icon_collection);
            for (int i = 0; i < icon_count; i++) {
                cJSON* icon = cJSON_GetArrayItem(icon_collection, i);
                if (cJSON_IsObject(icon)) {
                    cJSON* name = cJSON_GetObjectItem(icon, "name");
                    cJSON* file = cJSON_GetObjectItem(icon, "file");

                    if (cJSON_IsString(name) && cJSON_IsString(file)) {
                        if (GetAssetData(file->valuestring, ptr, size)) {
                            emote_display->AddIconData(name->valuestring, ptr, size);
                        } else {
                            ESP_LOGE(TAG, "Icon \"%10s\" image file %s is not found", name->valuestring, file->valuestring);
                        }
                    }
                }
            }
        }
    }

    cJSON* layout_json = cJSON_GetObjectItem(root, "layout");
    if (cJSON_IsArray(layout_json)) {
        int layout_count = cJSON_GetArraySize(layout_json);

        for (int i = 0; i < layout_count; i++) {
            cJSON* layout_item = cJSON_GetArrayItem(layout_json, i);
            if (cJSON_IsObject(layout_item)) {
                cJSON* name = cJSON_GetObjectItem(layout_item, "name");
                cJSON* align = cJSON_GetObjectItem(layout_item, "align");
                cJSON* x = cJSON_GetObjectItem(layout_item, "x");
                cJSON* y = cJSON_GetObjectItem(layout_item, "y");
                cJSON* width = cJSON_GetObjectItem(layout_item, "width");
                cJSON* height = cJSON_GetObjectItem(layout_item, "height");

                if (cJSON_IsString(name) && cJSON_IsString(align) && cJSON_IsNumber(x) && cJSON_IsNumber(y)) {
                    int width_val = cJSON_IsNumber(width) ? width->valueint : 0;
                    int height_val = cJSON_IsNumber(height) ? height->valueint : 0;

                    if (emote_display) {
                        emote_display->AddLayoutData(name->valuestring, align->valuestring,
                                                     x->valueint, y->valueint, width_val, height_val);
                    }
                } else {
                    ESP_LOGW(TAG, "Invalid layout item %d: missing required fields", i);
                }
            }
        }
    }
#endif

    cJSON_Delete(root);
    return true;
}

bool Assets::Download(std::string url, std::function<void(int progress, size_t speed)> progress_callback) {
    ESP_LOGI(TAG, "Downloading new version of assets from %s", url.c_str());
    
    // 取消当前资源分区的内存映射
    if (mmap_handle_ != 0) {
        esp_partition_munmap(mmap_handle_);
        mmap_handle_ = 0;
        mmap_root_ = nullptr;
    }
    checksum_valid_ = false;
    assets_.clear();

    // 下载新的资源文件
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to get assets, status code: %d", http->GetStatusCode());
        return false;
    }

    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        return false;
    }

    if (content_length > partition_->size) {
        ESP_LOGE(TAG, "Assets file size (%u) is larger than partition size (%lu)", content_length, partition_->size);
        return false;
    }

    // 定义扇区大小为4KB（ESP32的标准扇区大小）
    const size_t SECTOR_SIZE = esp_partition_get_main_flash_sector_size();
    
    // 计算需要擦除的扇区数量
    size_t sectors_to_erase = (content_length + SECTOR_SIZE - 1) / SECTOR_SIZE; // 向上取整
    size_t total_erase_size = sectors_to_erase * SECTOR_SIZE;
    
    ESP_LOGI(TAG, "Sector size: %u, content length: %u, sectors to erase: %u, total erase size: %u", 
             SECTOR_SIZE, content_length, sectors_to_erase, total_erase_size);
    
    // 写入新的资源文件到分区，一边erase一边写入
    char buffer[512];
    size_t total_written = 0;
    size_t recent_written = 0;
    size_t current_sector = 0;
    auto last_calc_time = esp_timer_get_time();
    
    while (true) {
        int ret = http->Read(buffer, sizeof(buffer));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            return false;
        }

        if (ret == 0) {
            break;
        }

        // 检查是否需要擦除新的扇区
        size_t write_end_offset = total_written + ret;
        size_t needed_sectors = (write_end_offset + SECTOR_SIZE - 1) / SECTOR_SIZE;
        
        // 擦除需要的新扇区
        while (current_sector < needed_sectors) {
            size_t sector_start = current_sector * SECTOR_SIZE;
            size_t sector_end = (current_sector + 1) * SECTOR_SIZE;
            
            // 确保擦除范围不超过分区大小
            if (sector_end > partition_->size) {
                ESP_LOGE(TAG, "Sector end (%u) exceeds partition size (%lu)", sector_end, partition_->size);
                return false;
            }
            
            ESP_LOGD(TAG, "Erasing sector %u (offset: %u, size: %u)", current_sector, sector_start, SECTOR_SIZE);
            esp_err_t err = esp_partition_erase_range(partition_, sector_start, SECTOR_SIZE);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to erase sector %u at offset %u: %s", current_sector, sector_start, esp_err_to_name(err));
                return false;
            }
            
            current_sector++;
        }

        // 写入数据到分区
        esp_err_t err = esp_partition_write(partition_, total_written, buffer, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write to assets partition at offset %u: %s", total_written, esp_err_to_name(err));
            return false;
        }

        total_written += ret;
        recent_written += ret;

        // 计算进度和速度
        if (esp_timer_get_time() - last_calc_time >= 1000000 || total_written == content_length || ret == 0) {
            size_t progress = total_written * 100 / content_length;
            size_t speed = recent_written; // 每秒的字节数
            ESP_LOGI(TAG, "Progress: %u%% (%u/%u), Speed: %u B/s, Sectors erased: %u", 
                     progress, total_written, content_length, speed, current_sector);
            if (progress_callback) {
                progress_callback(progress, speed);
            }
            last_calc_time = esp_timer_get_time();
            recent_written = 0; // 重置最近写入的字节数
        }
    }
    
    http->Close();

    if (total_written != content_length) {
        ESP_LOGE(TAG, "Downloaded size (%u) does not match expected size (%u)", total_written, content_length);
        return false;
    }

    ESP_LOGI(TAG, "Assets download completed, total written: %u bytes, total sectors erased: %u", 
             total_written, current_sector);

    // 重新初始化资源分区
    if (!InitializePartition()) {
        ESP_LOGE(TAG, "Failed to re-initialize assets partition");
        return false;
    }

    return true;
}

bool Assets::GetAssetData(const std::string& name, void*& ptr, size_t& size) {
    auto asset = assets_.find(name);
    if (asset == assets_.end()) {
        return false;
    }
    auto data = (const char*)(mmap_root_ + asset->second.offset);
    if (data[0] != 'Z' || data[1] != 'Z') {
        ESP_LOGE(TAG, "The asset %s is not valid with magic %02x%02x", name.c_str(), data[0], data[1]);
        return false;
    }

    ptr = static_cast<void*>(const_cast<char*>(data + 2));
    size = asset->second.size;
    return true;
}
