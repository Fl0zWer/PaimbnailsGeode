#include "ModerationService.hpp"
#include "../../../utils/HttpClient.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <fstream>

using namespace geode::prelude;

bool ModerationService::tryModCache(ModeratorCallback& callback) {
    if (!m_modCache.has_value()) return false;
    auto elapsed = std::chrono::steady_clock::now() - m_modCache->timestamp;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < MOD_CACHE_TTL_SECONDS) {
        callback(m_modCache->isMod, m_modCache->isAdmin);
        return true;
    }
    m_modCache.reset();
    return false;
}

void ModerationService::updateModCache(bool isMod, bool isAdmin) {
    m_modCache = ModCacheEntry{isMod || isAdmin, isAdmin, std::chrono::steady_clock::now()};
}

void ModerationService::checkModerator(std::string const& username, ModeratorCallback callback) {
    if (!m_serverEnabled) { callback(false, false); return; }
    if (tryModCache(callback)) return;

    int currentAccountID = GJAccountManager::get()->m_accountID;
    if (currentAccountID <= 0) {
        log::warn("[ModService] usuario '{}' no logueado, chequeo denegado", username);
        callback(false, false);
        return;
    }

    HttpClient::get().checkModeratorAccount(username, currentAccountID,
        [this, callback, username](bool isMod, bool isAdmin) {
            bool effectiveMod = isMod || isAdmin;
            updateModCache(effectiveMod, isAdmin);
            if (isAdmin) {
                Mod::get()->setSavedValue<bool>("is-verified-admin", true);
                auto path = Mod::get()->getSaveDir() / "admin_verification.dat";
                std::ofstream f(path, std::ios::binary | std::ios::trunc);
                if (f) {
                    time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    f.write(reinterpret_cast<char const*>(&now), sizeof(now));
                }
            }
            if (effectiveMod) {
                Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
                auto path = Mod::get()->getSaveDir() / "moderator_verification.dat";
                std::ofstream f(path, std::ios::binary | std::ios::trunc);
                if (f) {
                    time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    f.write(reinterpret_cast<char const*>(&now), sizeof(now));
                }
            }
            callback(effectiveMod, isAdmin);
        });
}

void ModerationService::checkModeratorAccount(std::string const& username, int accountID, ModeratorCallback callback) {
    if (!m_serverEnabled) { callback(false, false); return; }
    if (tryModCache(callback)) return;

    int currentAccountID = GJAccountManager::get()->m_accountID;
    if (currentAccountID <= 0) {
        log::warn("[ModService] usuario '{}' no logueado, chequeo denegado", username);
        callback(false, false);
        return;
    }

    HttpClient::get().checkModeratorAccount(username, currentAccountID,
        [this, callback](bool isMod, bool isAdmin) {
            bool effectiveMod = isMod || isAdmin;
            updateModCache(effectiveMod, isAdmin);
            if (isAdmin) Mod::get()->setSavedValue<bool>("is-verified-admin", true);
            if (effectiveMod) Mod::get()->setSavedValue<bool>("is-verified-moderator", true);
            callback(effectiveMod, isAdmin);
        });
}

void ModerationService::checkUserStatus(std::string const& username, ModeratorCallback callback) {
    if (!m_serverEnabled) { callback(false, false); return; }
    HttpClient::get().checkModerator(username, callback);
}

void ModerationService::addModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", GJAccountManager::get()->m_accountID}
    });

    HttpClient::get().postWithAuth("/api/admin/add-moderator", json.dump(),
        [callback](bool success, std::string const& response) {
            callback(success, success ? "moderador anadido con exito" : response);
        });
}

void ModerationService::removeModerator(std::string const& username, std::string const& adminUser, ActionCallback callback) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }

    matjson::Value json = matjson::makeObject({
        {"username", username},
        {"adminUser", adminUser},
        {"accountID", GJAccountManager::get()->m_accountID}
    });

    HttpClient::get().postWithAuth("/api/admin/remove-moderator", json.dump(),
        [callback](bool success, std::string const& response) {
            callback(success, success ? "moderador eliminado con exito" : response);
        });
}

void ModerationService::syncVerificationQueue(PendingCategory category, QueueCallback callback) {
    if (!m_serverEnabled) {
        callback(true, PendingQueue::get().list(category));
        return;
    }

    std::string endpoint = "/api/queue/";
    switch (category) {
        case PendingCategory::Verify:            endpoint += "verify";            break;
        case PendingCategory::Update:            endpoint += "update";            break;
        case PendingCategory::Report:            endpoint += "report";            break;
        case PendingCategory::ProfileBackground: endpoint += "profilebackground"; break;
        case PendingCategory::ProfileImg:        endpoint += "profileimgs";       break;
    }

    std::string username;
    int accountID = 0;
    if (auto* gm = GameManager::get()) username = gm->m_playerName;
    if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    if (!username.empty() && accountID > 0) {
        endpoint += "?username=" + HttpClient::encodeQueryParam(username)
                  + "&accountID=" + std::to_string(accountID);
    }

    HttpClient::get().get(endpoint, [callback, category](bool success, std::string const& response) {
        if (!success) { callback(true, PendingQueue::get().list(category)); return; }

        auto jsonRes = matjson::parse(response);
        if (!jsonRes.isOk()) { callback(true, PendingQueue::get().list(category)); return; }
        auto json = jsonRes.unwrap();

        if (!json.contains("items") || !json["items"].isArray()) {
            callback(true, PendingQueue::get().list(category));
            return;
        }
        auto itemsRes = json["items"].asArray();
        if (!itemsRes) { callback(true, PendingQueue::get().list(category)); return; }

        std::vector<PendingItem> items;
        for (auto const& item : itemsRes.unwrap()) {
            PendingItem it{};

            // levelId
            if (item.contains("levelId")) {
                if (item["levelId"].isString())
                    it.levelID = geode::utils::numFromString<int>(item["levelId"].asString().unwrapOr("0")).unwrapOr(0);
                else if (item["levelId"].isNumber())
                    it.levelID = item["levelId"].asInt().unwrapOr(0);
            }
            if (it.levelID == 0 && item.contains("accountID")) {
                if (item["accountID"].isString())
                    it.levelID = geode::utils::numFromString<int>(item["accountID"].asString().unwrapOr("0")).unwrapOr(0);
                else if (item["accountID"].isNumber())
                    it.levelID = item["accountID"].asInt().unwrapOr(0);
            }

            it.category = category;

            // timestamp ms → s
            if (item.contains("timestamp")) {
                long long ms = 0;
                if (item["timestamp"].isString())
                    ms = geode::utils::numFromString<long long>(item["timestamp"].asString().unwrapOr("0")).unwrapOr(0);
                else if (item["timestamp"].isNumber())
                    ms = (long long)item["timestamp"].asDouble().unwrapOr(0.0);
                it.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
            }

            if (item.contains("submittedBy") && item["submittedBy"].isString())
                it.submittedBy = item["submittedBy"].asString().unwrapOr("");
            if (item.contains("note") && item["note"].isString())
                it.note = item["note"].asString().unwrapOr("");
            if (item.contains("claimedBy") && item["claimedBy"].isString())
                it.claimedBy = item["claimedBy"].asString().unwrapOr("");

            it.status    = PendingStatus::Open;
            it.isCreator = false;

            // suggestions array
            if (item.contains("suggestions") && item["suggestions"].isArray()) {
                auto sugArr = item["suggestions"].asArray();
                if (sugArr.isOk()) {
                    for (auto const& sug : sugArr.unwrap()) {
                        Suggestion s;
                        if (sug.contains("filename") && sug["filename"].isString())
                            s.filename = sug["filename"].asString().unwrapOr("");
                        if (sug.contains("submittedBy") && sug["submittedBy"].isString())
                            s.submittedBy = sug["submittedBy"].asString().unwrapOr("");
                        if (sug.contains("timestamp") && sug["timestamp"].isNumber()) {
                            long long ms = (long long)sug["timestamp"].asDouble().unwrapOr(0.0);
                            s.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                        }
                        if (sug.contains("accountID") && sug["accountID"].isNumber())
                            s.accountID = sug["accountID"].asInt().unwrapOr(0);
                        it.suggestions.push_back(s);
                    }
                }
            } else if (it.category == PendingCategory::Verify) {
                Suggestion s;
                s.filename    = fmt::format("suggestions/{}.webp", it.levelID);
                s.submittedBy = it.submittedBy;
                s.timestamp   = it.timestamp;
                it.suggestions.push_back(s);
            } else if (it.category == PendingCategory::ProfileBackground) {
                if (item.contains("filename") && item["filename"].isString()) {
                    Suggestion s;
                    s.filename    = item["filename"].asString().unwrapOr("");
                    s.submittedBy = it.submittedBy;
                    s.timestamp   = it.timestamp;
                    if (!s.filename.empty()) it.suggestions.push_back(s);
                }
            } else if (it.category == PendingCategory::ProfileImg) {
                if (item.contains("filename") && item["filename"].isString()) {
                    Suggestion s;
                    s.filename    = item["filename"].asString().unwrapOr("");
                    s.submittedBy = it.submittedBy;
                    s.timestamp   = it.timestamp;
                    if (!s.filename.empty()) it.suggestions.push_back(s);
                }
            }

            // user report fields
            if (item.contains("type") && item["type"].isString())
                it.type = item["type"].asString().unwrapOr("");
            if (item.contains("reportedUsername") && item["reportedUsername"].isString())
                it.reportedUsername = item["reportedUsername"].asString().unwrapOr("");
            if (item.contains("reports") && item["reports"].isArray()) {
                auto repArr = item["reports"].asArray();
                if (repArr.isOk()) {
                    for (auto const& rpt : repArr.unwrap()) {
                        ReportEntry re;
                        if (rpt.contains("reporter") && rpt["reporter"].isString())
                            re.reporter = rpt["reporter"].asString().unwrapOr("");
                        if (rpt.contains("reporterAccountID") && rpt["reporterAccountID"].isNumber())
                            re.reporterAccountID = rpt["reporterAccountID"].asInt().unwrapOr(0);
                        if (rpt.contains("note") && rpt["note"].isString())
                            re.note = rpt["note"].asString().unwrapOr("");
                        if (rpt.contains("timestamp") && rpt["timestamp"].isNumber()) {
                            long long ms = (long long)rpt["timestamp"].asDouble().unwrapOr(0.0);
                            re.timestamp = (int64_t)(ms > 0 ? (ms / 1000) : 0);
                        }
                        it.reports.push_back(re);
                    }
                }
            }

            if (it.levelID != 0) items.push_back(std::move(it));
        }
        callback(true, items);
    });
}

void ModerationService::claimQueueItem(int levelId, PendingCategory category,
                                       std::string const& username, ActionCallback callback,
                                       std::string const& type) {
    if (!m_serverEnabled) { callback(false, "servidor desactivado"); return; }

    int accountID = 0;
    if (auto* am = GJAccountManager::get()) accountID = am->m_accountID;
    if (username.empty() || accountID <= 0) { callback(false, "Account ID required"); return; }

    std::string endpoint = fmt::format("/api/queue/claim/{}", levelId);
    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"accountID", accountID}
    });
    if (!type.empty()) json["type"] = type;
    std::string postData = json.dump();

    HttpClient::get().checkModeratorAccount(username, accountID,
        [this, callback, levelId, username, accountID, endpoint, postData](bool isMod, bool isAdmin) {
            if (!(isMod || isAdmin)) { callback(false, "No tienes permisos de moderador"); return; }

            HttpClient::get().postWithAuth(endpoint, postData,
                [this, callback, levelId, username, accountID, endpoint, postData](bool success, std::string const& response) {
                    if (success) { callback(true, response); return; }

                    bool authFailed = response.find("403") != std::string::npos ||
                                     response.find("needsModCode") != std::string::npos ||
                                     response.find("invalidCode") != std::string::npos ||
                                     response.find("Moderator auth required") != std::string::npos;
                    if (!authFailed) { callback(false, response); return; }

                    // retry: refresh mod code and try once more
                    m_modCache.reset();
                    HttpClient::get().checkModeratorAccount(username, accountID,
                        [callback, endpoint, postData](bool isMod2, bool isAdmin2) {
                            if (!(isMod2 || isAdmin2)) { callback(false, "Mod Code invalido. Genera uno nuevo en ajustes."); return; }

                            HttpClient::get().postWithAuth(endpoint, postData,
                                [callback](bool retryOk, std::string const& retryResp) {
                                    if (retryOk) { callback(true, retryResp); return; }
                                    if (retryResp.find("needsModCode") != std::string::npos)
                                        callback(false, "Configura tu Mod Code en ajustes de Paimbnails");
                                    else if (retryResp.find("invalidCode") != std::string::npos)
                                        callback(false, "Mod Code invalido o expirado. Actualiza en ajustes.");
                                    else
                                        callback(false, retryResp);
                                });
                        });
                });
        });
}

void ModerationService::acceptQueueItem(int levelId, PendingCategory category,
                                        std::string const& username, ActionCallback callback,
                                        std::string const& targetFilename,
                                        std::string const& type) {
    if (!m_serverEnabled) {
        PendingQueue::get().accept(levelId, category);
        callback(true, "aceptado localmente");
        return;
    }

    int accountID = GJAccountManager::get()->m_accountID;
    std::string endpoint = fmt::format("/api/queue/accept/{}", levelId);

    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"accountID", accountID}
    });
    if (!targetFilename.empty()) json["targetFilename"] = targetFilename;
    if (!type.empty()) json["type"] = type;
    std::string postData = json.dump();

    HttpClient::get().checkModeratorAccount(username, accountID,
        [this, callback, levelId, category, username, accountID, endpoint, postData](bool isMod, bool isAdmin) {
            if (!(isMod || isAdmin)) { callback(false, "No tienes permisos de moderador"); return; }

            HttpClient::get().postWithAuth(endpoint, postData,
                [this, callback, levelId, category, username, accountID, endpoint, postData](bool success, std::string const& response) {
                    if (success) {
                        PendingQueue::get().accept(levelId, category);
                        callback(true, response);
                        return;
                    }

                    bool authFailed = response.find("403") != std::string::npos ||
                                     response.find("needsModCode") != std::string::npos ||
                                     response.find("invalidCode") != std::string::npos ||
                                     response.find("Moderator auth required") != std::string::npos;
                    if (!authFailed) { callback(false, response); return; }

                    m_modCache.reset();
                    HttpClient::get().checkModeratorAccount(username, accountID,
                        [callback, levelId, category, endpoint, postData](bool isMod2, bool isAdmin2) {
                            if (!(isMod2 || isAdmin2)) { callback(false, "Mod Code invalido. Genera uno nuevo en ajustes."); return; }

                            HttpClient::get().postWithAuth(endpoint, postData,
                                [callback, levelId, category](bool retryOk, std::string const& retryResp) {
                                    if (retryOk) {
                                        PendingQueue::get().accept(levelId, category);
                                        callback(true, retryResp);
                                    } else {
                                        if (retryResp.find("needsModCode") != std::string::npos)
                                            callback(false, "Configura tu Mod Code en ajustes de Paimbnails");
                                        else if (retryResp.find("invalidCode") != std::string::npos)
                                            callback(false, "Mod Code invalido o expirado. Actualiza en ajustes.");
                                        else
                                            callback(false, retryResp);
                                    }
                                });
                        });
                });
        });
}

void ModerationService::rejectQueueItem(int levelId, PendingCategory category,
                                        std::string const& username, std::string const& reason,
                                        ActionCallback callback,
                                        std::string const& type) {
    if (!m_serverEnabled) {
        PendingQueue::get().reject(levelId, category, reason);
        callback(true, "rechazado localmente");
        return;
    }

    int accountID = GJAccountManager::get()->m_accountID;
    std::string endpoint = fmt::format("/api/queue/reject/{}", levelId);

    matjson::Value json = matjson::makeObject({
        {"levelId", levelId},
        {"category", PendingQueue::catToStr(category)},
        {"username", username},
        {"reason", reason},
        {"accountID", accountID}
    });
    if (!type.empty()) json["type"] = type;
    std::string postData = json.dump();

    HttpClient::get().checkModeratorAccount(username, accountID,
        [this, callback, levelId, category, reason, username, accountID, endpoint, postData](bool isMod, bool isAdmin) {
            if (!(isMod || isAdmin)) { callback(false, "No tienes permisos de moderador"); return; }

            HttpClient::get().postWithAuth(endpoint, postData,
                [this, callback, levelId, category, reason, username, accountID, endpoint, postData](bool success, std::string const& response) {
                    if (success) {
                        PendingQueue::get().reject(levelId, category, reason);
                        callback(true, response);
                        return;
                    }

                    bool authFailed = response.find("403") != std::string::npos ||
                                     response.find("needsModCode") != std::string::npos ||
                                     response.find("invalidCode") != std::string::npos ||
                                     response.find("Moderator auth required") != std::string::npos;
                    if (!authFailed) { callback(false, response); return; }

                    m_modCache.reset();
                    HttpClient::get().checkModeratorAccount(username, accountID,
                        [callback, levelId, category, reason, endpoint, postData](bool isMod2, bool isAdmin2) {
                            if (!(isMod2 || isAdmin2)) { callback(false, "Mod Code invalido. Genera uno nuevo en ajustes."); return; }

                            HttpClient::get().postWithAuth(endpoint, postData,
                                [callback, levelId, category, reason](bool retryOk, std::string const& retryResp) {
                                    if (retryOk) {
                                        PendingQueue::get().reject(levelId, category, reason);
                                        callback(true, retryResp);
                                    } else {
                                        if (retryResp.find("needsModCode") != std::string::npos)
                                            callback(false, "Configura tu Mod Code en ajustes de Paimbnails");
                                        else if (retryResp.find("invalidCode") != std::string::npos)
                                            callback(false, "Mod Code invalido o expirado. Actualiza en ajustes.");
                                        else
                                            callback(false, retryResp);
                                    }
                                });
                        });
                });
        });
}

void ModerationService::submitReport(int levelId, std::string const& username,
                                     std::string const& note, ActionCallback callback) {
    if (!m_serverEnabled) {
        PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
        callback(true, "reportado localmente");
        return;
    }

    HttpClient::get().submitReport(levelId, username, note,
        [callback, levelId, username, note](bool success, std::string const& response) {
            if (success) PendingQueue::get().addOrBump(levelId, PendingCategory::Report, username, note);
            callback(success, response);
        });
}
