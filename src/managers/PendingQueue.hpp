#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

enum class PendingCategory { Verify, Update, Report, Banner };
enum class PendingStatus { Open, Accepted, Rejected };

struct Suggestion {
    std::string filename;
    std::string submittedBy;
    int64_t timestamp = 0;
    int accountID = 0;
};

struct PendingItem {
    int levelID = 0;
    PendingCategory category = PendingCategory::Verify;
    int64_t timestamp = 0; // segundos unix
    std::string submittedBy; // nombre usuario GD si disponible
    std::string note;        // comentario/motivo opcional
    std::string claimedBy;   // moderador que reclamo el nivel
    PendingStatus status = PendingStatus::Open;
    bool isCreator = false;  // verdadero si subido por creador nivel
    
    std::vector<Suggestion> suggestions;
};

class PendingQueue {
public:
    static PendingQueue& get();

    // anadir o actualizar item; si existe item abierto para mismo nivel+categoria, actualizar timestamp y nota/usuario
    void addOrBump(int levelID, PendingCategory cat, std::string submittedBy = {}, std::string note = {}, bool isCreator = false);

    // eliminar items (cualquier categoria) para nivel aun abierto
    void removeForLevel(int levelID);

    // marcar item rechazado (y ocultar de lista)
    void reject(int levelID, PendingCategory cat, std::string reason = {});

    // marcar aceptado (usado si aceptado fuera callback subida)
    void accept(int levelID, PendingCategory cat);

    // listar items abiertos por categoria
    std::vector<PendingItem> list(PendingCategory cat) const;

    // persistir localmente
    void load();
    void save();

    // serializar estado cola a JSON para sync servidor
    std::string toJson() const;

    // llamar en cambio; sync debounced a servidor
    void syncNow();
    
    // hacer catToStr publico para acceso ThumbnailAPI
    static const char* catToStr(PendingCategory c);
    
    // auxiliar para verificar si usuario es creador nivel
    static bool isLevelCreator(GJGameLevel* level, const std::string& username);

private:
    PendingQueue() = default;
    std::filesystem::path jsonPath() const;
    static PendingCategory strToCat(std::string const& s);
    static const char* statusToStr(PendingStatus s);
    static PendingStatus strToStatus(std::string const& s);
    static std::string escape(const std::string& s);

    bool m_loaded = false;
    mutable std::vector<PendingItem> m_items; // incluye no-abiertos para historial
};

