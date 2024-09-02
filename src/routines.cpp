#define R_NO_REMAP

#include "R.h"
#include "Rinternals.h"
#include "R_ext/Rdynload.h"

#include "rtools.hpp"
#include "xeus-r/xinterpreter.hpp"
#include "nlohmann/json.hpp"
#include "xeus/xmessage.hpp"
#include "xeus/xcomm.hpp"
#include "xeus/xlogger.hpp"

#include <functional>

namespace xeus_r {
namespace routines {

SEXP kernel_info_request() {
    auto info = xeus_r::get_interpreter()->kernel_info_request();
    SEXP out = PROTECT(Rf_mkString(info.dump(4).c_str()));
    Rf_classgets(out, Rf_mkString("json"));
    UNPROTECT(1);
    return out;
}

SEXP publish_stream(SEXP name_, SEXP text_) {
    auto name = CHAR(STRING_ELT(name_, 0));
    auto text = CHAR(STRING_ELT(text_, 0));

    auto interpreter = xeus_r::get_interpreter();
    interpreter->publish_stream(name, text);

    return R_NilValue;
}

SEXP display_data(SEXP js_data, SEXP js_metadata){
    auto data = nl::json::parse(CHAR(STRING_ELT(js_data, 0)));
    auto metadata = nl::json::parse(CHAR(STRING_ELT(js_metadata, 0)));
    
    xeus_r::get_interpreter()->display_data(
        std::move(data), std::move(metadata), /* transient = */ nl::json::object()
    );

    return R_NilValue;
}

SEXP update_display_data(SEXP js_data, SEXP js_metadata){
    auto data = nl::json::parse(CHAR(STRING_ELT(js_data, 0)));
    auto metadata = nl::json::parse(CHAR(STRING_ELT(js_metadata, 0)));
    
    xeus_r::get_interpreter()->update_display_data(
        std::move(data), std::move(metadata), /* transient = */ nl::json::object()
    );

    return R_NilValue;
}

SEXP clear_output(SEXP wait_) {
    bool wait = LOGICAL_ELT(wait_, 0) == TRUE;
    xeus_r::get_interpreter()->clear_output(wait);
    return R_NilValue;
}

SEXP is_complete_request(SEXP code_) {
    std::string code = CHAR(STRING_ELT(code_, 0));
    auto is_complete = xeus_r::get_interpreter()->is_complete_request(code);

    SEXP out = PROTECT(Rf_mkString(is_complete.dump(4).c_str()));
    Rf_classgets(out, Rf_mkString("json"));
    UNPROTECT(1);
    return out;
}

SEXP xeusr_log(SEXP level_, SEXP msg_) {
    std::string level = CHAR(STRING_ELT(level_, 0));
    std::string msg = CHAR(STRING_ELT(msg_, 0));

    // TODO: actually do some logging
    return R_NilValue;
}

SEXP CommManager__register_target(SEXP name_) {
    using namespace xeus_r;

    std::string name = CHAR(STRING_ELT(name_, 0));
    
    auto callback = [name](xeus::xcomm&& comm, xeus::xmessage request) {
        SEXP comm_id = PROTECT(Rf_mkString(comm.id().c_str()));

        auto ptr_request = new xeus::xmessage(std::move(request));
        SEXP xptr_request = PROTECT(R_MakeExternalPtr(
            reinterpret_cast<void*>(ptr_request), R_NilValue, R_NilValue
        ));
        R_RegisterCFinalizerEx(xptr_request, [](SEXP xp) {
            delete reinterpret_cast<xeus::xmessage*>(R_ExternalPtrAddr(xp));
        }, FALSE);
        
        r::invoke_xeusr_fn("CommManager__register_target_callback", comm_id, xptr_request);

        UNPROTECT(2);
    };

    get_interpreter()->comm_manager().register_comm_target(name, callback);
    return R_NilValue;
}

SEXP CommManager__unregister_target(SEXP name_) {
    std::string name = CHAR(STRING_ELT(name_, 0));

    xeus_r::get_interpreter()->comm_manager().unregister_comm_target(name);
    return R_NilValue;
}

SEXP CommManager__get_comm(SEXP id_) {
    auto id = xeus::xguid(CHAR(STRING_ELT(id_, 0)));

    auto comms = get_interpreter()->comm_manager().comms() ;
    auto it = comms.find(id);
    if (it == comms.end()) {
        return R_NilValue;
    }

    SEXP xp = PROTECT(R_MakeExternalPtr(
        reinterpret_cast<void*>(it->second), R_NilValue, R_NilValue
    ));
    UNPROTECT(1);
    return xp;
}

SEXP CommManager__new_comm(SEXP target_name_) {
    auto target = get_interpreter()->comm_manager().target(CHAR(STRING_ELT(target_name_, 0)));
    if (target == nullptr) {
        return R_NilValue;
    }

    auto id = xeus::new_xguid();
    auto comm = new xeus::xcomm(target, id);
    SEXP xp_comm = PROTECT(R_MakeExternalPtr(
        reinterpret_cast<void*>(comm), R_NilValue, R_NilValue
    ));
    R_RegisterCFinalizerEx(xp_comm, [](SEXP xp) {
        delete reinterpret_cast<xeus::xmessage*>(R_ExternalPtrAddr(xp));
    }, FALSE);

    UNPROTECT(1);

    return xp_comm;
}

SEXP Comm__id(SEXP xp_comm) {
    auto comm = reinterpret_cast<xeus::xcomm*>(R_ExternalPtrAddr(xp_comm));
    return Rf_mkString(comm->id().c_str());
}

SEXP Comm__target_name(SEXP xp_comm) {
    auto comm = reinterpret_cast<xeus::xcomm*>(R_ExternalPtrAddr(xp_comm));
    return Rf_mkString(comm->target().name().c_str());
}

SEXP xmessage__get_content(SEXP xptr_msg) {
    auto ptr_msg = reinterpret_cast<xeus::xmessage*>(R_ExternalPtrAddr(xptr_msg));
    
    SEXP out = PROTECT(Rf_mkString(ptr_msg->content().dump(4).c_str()));
    Rf_classgets(out, Rf_mkString("json"));
    UNPROTECT(1);
    
    return out;
}

}

#ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
void register_r_routines() {
    DllInfo *info = R_getEmbeddingDllInfo();

    static const R_CallMethodDef callMethods[]  = {
        {"xeusr_kernel_info_request"     , (DL_FUNC) &routines::kernel_info_request     , 0},
        {"xeusr_publish_stream"          , (DL_FUNC) &routines::publish_stream          , 2},
        {"xeusr_display_data"            , (DL_FUNC) &routines::display_data            , 2},
        {"xeusr_update_display_data"     , (DL_FUNC) &routines::update_display_data     , 2},
        {"xeusr_clear_output"            , (DL_FUNC) &routines::clear_output            , 1},
        {"xeusr_is_complete_request"     , (DL_FUNC) &routines::is_complete_request     , 1},
        {"xeusr_log"                     , (DL_FUNC) &routines::xeusr_log               , 2},

        // comms
        {"CommManager__register_target"    , (DL_FUNC) &routines::CommManager__register_target, 1},
        {"CommManager__unregister_target"  , (DL_FUNC) &routines::CommManager__unregister_target, 1},

        {"CommManager__get_comm"    , (DL_FUNC) &routines::CommManager__get_comm, 1},
        
        {"Comm__id"    , (DL_FUNC) &routines::Comm__id, 1},
        {"Comm__target_name"    , (DL_FUNC) &routines::Comm__target_name, 1},

        // xeus::xmessage
        {"xeusr_xmessage__get_content"    , (DL_FUNC) &routines::xmessage__get_content, 1},

        {NULL, NULL, 0}
    };

    R_registerRoutines(info, NULL, callMethods, NULL, NULL);
}
#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif

}
