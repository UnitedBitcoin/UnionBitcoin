#include <uvm/btc_uvm_api.h>

#include <uvm/lprefix.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string>
#include <sstream>
#include <utility>
#include <list>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <uvm/uvm_api.h>
#include <uvm/uvm_lib.h>
#include <uvm/uvm_lutil.h>
#include <uvm/lobject.h>
#include <uvm/lstate.h>

#include <validation.h>

namespace uvm {
    namespace lua {
        namespace api {

            static int has_error = 0;

            static std::string get_file_name_str_from_contract_module_name(std::string name)
            {
                std::stringstream ss;
                ss << "uvm_contract_" << name;
                return ss.str();
            }

            /**
            * whether exception happen in L
            */
            bool BtcUvmChainApi::has_exception(lua_State *L)
            {
                return has_error ? true : false;
            }

            /**
            * clear exception marked
            */
            void BtcUvmChainApi::clear_exceptions(lua_State *L)
            {
                has_error = 0;
            }

            /**
            * when exception happened, use this api to tell uvm
            * @param L the lua stack
            * @param code error code, 0 is OK, other is different error
            * @param error_format error info string, will be released by lua
            * @param ... error arguments
            */
            void BtcUvmChainApi::throw_exception(lua_State *L, int code, const char *error_format, ...)
            {
                has_error = 1;
                char *msg = (char*)lua_malloc(L, LUA_EXCEPTION_MULTILINE_STRNG_MAX_LENGTH);
                memset(msg, 0x0, LUA_EXCEPTION_MULTILINE_STRNG_MAX_LENGTH);

                va_list vap;
                va_start(vap, error_format);
                vsnprintf(msg, LUA_EXCEPTION_MULTILINE_STRNG_MAX_LENGTH, error_format, vap);
                va_end(vap);
                if (strlen(msg) > LUA_EXCEPTION_MULTILINE_STRNG_MAX_LENGTH - 1)
                {
                    msg[LUA_EXCEPTION_MULTILINE_STRNG_MAX_LENGTH - 1] = 0;
                }
                lua_set_compile_error(L, msg);

                //如果上次的exception code为uvm_API_LVM_LIMIT_OVER_ERROR, 不能被其他异常覆盖
                //只有调用clear清理后，才能继续记录异常
                int last_code = uvm::lua::lib::get_lua_state_value(L, "exception_code").int_value;
                if (last_code != code && last_code != 0)
                {
                    return;
                }

                GluaStateValue val_code;
                val_code.int_value = code;

                GluaStateValue val_msg;
                val_msg.string_value = msg;

                uvm::lua::lib::set_lua_state_value(L, "exception_code", val_code, GluaStateValueType::LUA_STATE_VALUE_INT);
                uvm::lua::lib::set_lua_state_value(L, "exception_msg", val_msg, GluaStateValueType::LUA_STATE_VALUE_STRING);
            }

            static ContractExec* get_evaluator(lua_State *L)
            {
                return (ContractExec*) uvm::lua::lib::get_lua_state_value(L, "evaluator").pointer_value;
            }

            /**
            * check whether the contract apis limit over, in this lua_State
            * @param L the lua stack
            * @return TRUE(1 or not 0) if over limit(will break the vm), FALSE(0) if not over limit
            */
            int BtcUvmChainApi::check_contract_api_instructions_over_limit(lua_State *L)
            {
                return 0; // TODO
            }

            int BtcUvmChainApi::get_stored_contract_info(lua_State *L, const char *name, std::shared_ptr<GluaContractInfo> contract_info_ret)
            {
                auto addr =  name;
                if(!contract_info_ret)
                    return 0;
                auto evaluator = get_evaluator(L);
                // find in contract create op
                for(const auto& ctx : evaluator->txs)
                {
                    if(ctx.opcode == OP_CREATE)
                    {
                        // create contract tx
                        if(ctx.params.contract_address == std::string(addr))
                        {
                            for(const auto& api : ctx.params.code.abi)
                            {
                                contract_info_ret->contract_apis.push_back(api);
                            }
                            for (const auto& api : ctx.params.code.offline_abi)
                            {
                                contract_info_ret->contract_apis.push_back(api);
                            }
                            return 1;
                        }
                    }
                }
                // TODO: find in pendingState/db
                return 0;
            }
            int BtcUvmChainApi::get_stored_contract_info_by_address(lua_State *L, const char *contract_id, std::shared_ptr<GluaContractInfo> contract_info_ret)
            {
                if(!contract_info_ret)
                    return 0;
                auto evaluator = get_evaluator(L);
                // find in contract create op
                for(const auto& ctx : evaluator->txs)
                {
                    if(ctx.opcode == OP_CREATE)
                    {
                        // create contract tx
                        if(ctx.params.contract_address == std::string(contract_id))
                        {
                            for(const auto& api : ctx.params.code.abi)
                            {
                                contract_info_ret->contract_apis.push_back(api);
                            }
                            for (const auto& api : ctx.params.code.offline_abi)
                            {
                                contract_info_ret->contract_apis.push_back(api);
                            }
                            return 1;
                        }
                    }
                }
                // TODO: find in pendingState/db
                return 0;
            }

            std::shared_ptr<GluaModuleByteStream> BtcUvmChainApi::get_bytestream_from_code(lua_State *L, const uvm::blockchain::Code& code)
            {
                if (code.code.size() > LUA_MODULE_BYTE_STREAM_BUF_SIZE)
                    return nullptr;
                auto p_luamodule = std::make_shared<GluaModuleByteStream>();
                p_luamodule->is_bytes = true;
                p_luamodule->buff.resize(code.code.size());
                memcpy(p_luamodule->buff.data(), code.code.data(), code.code.size());
                p_luamodule->contract_name = "";

                p_luamodule->contract_apis.clear();
                std::copy(code.abi.begin(), code.abi.end(), std::back_inserter(p_luamodule->contract_apis));

                p_luamodule->contract_emit_events.clear();
                std::copy(code.offline_abi.begin(), code.offline_abi.end(), std::back_inserter(p_luamodule->offline_apis));

                p_luamodule->contract_emit_events.clear();
                std::copy(code.events.begin(), code.events.end(), std::back_inserter(p_luamodule->contract_emit_events));

                p_luamodule->contract_storage_properties.clear();
                for (const auto &p : code.storage_properties)
                {
                    p_luamodule->contract_storage_properties[p.first] = p.second;
                }
                return p_luamodule;
            }

            void BtcUvmChainApi::get_contract_address_by_name(lua_State *L, const char *name, char *address, size_t *address_size)
            {
                // TODO
                std::string contract_name = uvm::lua::lib::unwrap_any_contract_name(name);
                strncpy(address, contract_name.c_str(), contract_name.size() + 1);
                if(address_size)
                    *address_size = strlen(address) + 1;
            }

            bool BtcUvmChainApi::check_contract_exist_by_address(lua_State *L, const char *address)
            {
                // TODO
                return true;
            }

            bool BtcUvmChainApi::check_contract_exist(lua_State *L, const char *name)
            {
                // TODO
                return true;
            }

            /**
            * load contract lua byte stream from uvm api
            */
            std::shared_ptr<GluaModuleByteStream> BtcUvmChainApi::open_contract(lua_State *L, const char *name)
            {
                // TODO
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                auto addr = name;
                auto evaluator = get_evaluator(L);
                // find in contract create op
                for(const auto& ctx : evaluator->txs)
                {
                    if(ctx.opcode == OP_CREATE)
                    {
                        // create contract tx
                        if(ctx.params.contract_address == std::string(addr))
                        {
                            const auto& code_val = ctx.params.code;
                            auto stream = std::make_shared<GluaModuleByteStream>();
                            if(nullptr == stream)
                                return nullptr;
                            stream->buff.resize(code_val.code.size());
                            memcpy(stream->buff.data(), code_val.code.data(), code_val.code.size());
                            stream->is_bytes = true;
                            stream->contract_name = name;
                            stream->contract_id = std::string(addr);
                            return stream;
                        }
                    }
                }
                return nullptr;
            }

            std::shared_ptr<GluaModuleByteStream> BtcUvmChainApi::open_contract_by_address(lua_State *L, const char *address)
            {
                // TODO
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                auto evaluator = get_evaluator(L);
                // find in contract create op
                for(const auto& ctx : evaluator->txs)
                {
                    if(ctx.opcode == OP_CREATE)
                    {
                        // create contract tx
                        if(ctx.params.contract_address == std::string(address))
                        {
                            const auto& code_val = ctx.params.code;
                            auto stream = std::make_shared<GluaModuleByteStream>();
                            if(nullptr == stream)
                                return nullptr;
                            stream->buff.resize(code_val.code.size());
                            memcpy(stream->buff.data(), code_val.code.data(), code_val.code.size());
                            stream->is_bytes = true;
                            stream->contract_name = "";
                            stream->contract_id = std::string(address);
                            return stream;
                        }
                    }
                }
                return nullptr;
            }

            GluaStorageValue BtcUvmChainApi::get_storage_value_from_uvm(lua_State *L, const char *contract_name, std::string name)
            {
                // TODO
                GluaStorageValue value;
                value.type = uvm::blockchain::StorageValueTypes::storage_value_null;
                value.value.int_value = 0;
                return value;
            }

            GluaStorageValue BtcUvmChainApi::get_storage_value_from_uvm_by_address(lua_State *L, const char *contract_address, std::string name)
            {
                // TODO
                GluaStorageValue value;
                value.type = uvm::blockchain::StorageValueTypes::storage_value_null;
                value.value.int_value = 0;
                return value;
            }

            bool BtcUvmChainApi::commit_storage_changes_to_uvm(lua_State *L, AllContractsChangesMap &changes)
            {
                // TODO
                return true;
            }

            intptr_t BtcUvmChainApi::register_object_in_pool(lua_State *L, intptr_t object_addr, GluaOutsideObjectTypes type)
            {
                auto node = uvm::lua::lib::get_lua_state_value_node(L, GLUA_OUTSIDE_OBJECT_POOLS_KEY);
                // Map<type, Map<object_key, object_addr>>
                std::map<GluaOutsideObjectTypes, std::shared_ptr<std::map<intptr_t, intptr_t>>> *object_pools = nullptr;
                if(node.type == GluaStateValueType::LUA_STATE_VALUE_nullptr)
                {
                    node.type = GluaStateValueType::LUA_STATE_VALUE_POINTER;
                    object_pools = new std::map<GluaOutsideObjectTypes, std::shared_ptr<std::map<intptr_t, intptr_t>>>();
                    node.value.pointer_value = (void*)object_pools;
                    uvm::lua::lib::set_lua_state_value(L, GLUA_OUTSIDE_OBJECT_POOLS_KEY, node.value, node.type);
                }
                else
                {
                    object_pools = (std::map<GluaOutsideObjectTypes, std::shared_ptr<std::map<intptr_t, intptr_t>>> *) node.value.pointer_value;
                }
                if(object_pools->find(type) == object_pools->end())
                {
                    object_pools->emplace(std::make_pair(type, std::make_shared<std::map<intptr_t, intptr_t>>()));
                }
                auto pool = (*object_pools)[type];
                auto object_key = object_addr;
                (*pool)[object_key] = object_addr;
                return object_key;
            }

            intptr_t BtcUvmChainApi::is_object_in_pool(lua_State *L, intptr_t object_key, GluaOutsideObjectTypes type)
            {
                auto node = uvm::lua::lib::get_lua_state_value_node(L, GLUA_OUTSIDE_OBJECT_POOLS_KEY);
                // Map<type, Map<object_key, object_addr>>
                std::map<GluaOutsideObjectTypes, std::shared_ptr<std::map<intptr_t, intptr_t>>> *object_pools = nullptr;
                if (node.type == GluaStateValueType::LUA_STATE_VALUE_nullptr)
                {
                    return 0;
                }
                else
                {
                    object_pools = (std::map<GluaOutsideObjectTypes, std::shared_ptr<std::map<intptr_t, intptr_t>>> *) node.value.pointer_value;
                }
                if (object_pools->find(type) == object_pools->end())
                {
                    object_pools->emplace(std::make_pair(type, std::make_shared<std::map<intptr_t, intptr_t>>()));
                }
                auto pool = (*object_pools)[type];
                return (*pool)[object_key];
            }

            void BtcUvmChainApi::release_objects_in_pool(lua_State *L)
            {
                auto node = uvm::lua::lib::get_lua_state_value_node(L, GLUA_OUTSIDE_OBJECT_POOLS_KEY);
                // Map<type, Map<object_key, object_addr>>
                std::map<GluaOutsideObjectTypes, std::shared_ptr<std::map<intptr_t, intptr_t>>> *object_pools = nullptr;
                if (node.type == GluaStateValueType::LUA_STATE_VALUE_nullptr)
                {
                    return;
                }
                object_pools = (std::map<GluaOutsideObjectTypes, std::shared_ptr<std::map<intptr_t, intptr_t>>> *) node.value.pointer_value;
                // TODO: 对于object_pools中不同类型的对象，分别释放
                for(const auto &p : *object_pools)
                {
                    auto type = p.first;
                    auto pool = p.second;
                    for(const auto &object_item : *pool)
                    {
                        auto object_key = object_item.first;
                        auto object_addr = object_item.second;
                        if (object_addr == 0)
                            continue;
                        switch(type)
                        {
                            case GluaOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE:
                            {
                                auto stream = (uvm::lua::lib::GluaByteStream*) object_addr;
                                delete stream;
                            } break;
                            default: {
                                continue;
                            }
                        }
                    }
                }
                delete object_pools;
                GluaStateValue null_state_value;
                null_state_value.int_value = 0;
                uvm::lua::lib::set_lua_state_value(L, GLUA_OUTSIDE_OBJECT_POOLS_KEY, null_state_value, GluaStateValueType::LUA_STATE_VALUE_nullptr);
            }

            bool BtcUvmChainApi::register_storage(lua_State *L, const char *contract_name, const char *name)
            {
                // printf("registered storage %s[%s] to uvm\n", contract_name, name);
                return true;
            }

            lua_Integer BtcUvmChainApi::transfer_from_contract_to_address(lua_State *L, const char *contract_address, const char *to_address,
                                                                            const char *asset_type, int64_t amount_str)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                printf("contract transfer from %s to %s, asset[%s] amount %ld\n", contract_address, to_address, asset_type, amount_str);
                return 0;
            }

            lua_Integer BtcUvmChainApi::transfer_from_contract_to_public_account(lua_State *L, const char *contract_address, const char *to_account_name,
                                                                                   const char *asset_type, int64_t amount)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                printf("contract transfer from %s to %s, asset[%s] amount %ld\n", contract_address, to_account_name, asset_type, amount);
                return 0;
            }

            int64_t BtcUvmChainApi::get_contract_balance_amount(lua_State *L, const char *contract_address, const char* asset_symbol)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return 0;
            }

            int64_t BtcUvmChainApi::get_transaction_fee(lua_State *L)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return 0;
            }

            uint32_t BtcUvmChainApi::get_chain_now(lua_State *L)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return 0;
            }

            uint32_t BtcUvmChainApi::get_chain_random(lua_State *L)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return 0;
            }

            std::string BtcUvmChainApi::get_transaction_id(lua_State *L)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return "";
            }

            uint32_t BtcUvmChainApi::get_header_block_num(lua_State *L)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return 0;
            }

            uint32_t BtcUvmChainApi::wait_for_future_random(lua_State *L, int next)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return 0;
            }

            int32_t BtcUvmChainApi::get_waited(lua_State *L, uint32_t num)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                return num;
            }

            void BtcUvmChainApi::emit(lua_State *L, const char* contract_id, const char* event_name, const char* event_param)
            {
                uvm::lua::lib::increment_lvm_instructions_executed_count(L, CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT - 1);
                printf("emit called\n");
            }

            bool BtcUvmChainApi::is_valid_address(lua_State *L, const char *address_str)
            {
                return true;
            }

            const char * BtcUvmChainApi::get_system_asset_symbol(lua_State *L)
            {
                return "COIN";
            }

        }
    }
}