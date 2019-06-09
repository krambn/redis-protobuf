/**************************************************************************
   Copyright (c) 2019 sewenew

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 *************************************************************************/

#include "append_command.h"
#include "errors.h"
#include "redis_protobuf.h"

namespace sw {

namespace redis {

namespace pb {

int AppendCommand::run(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) const {
    try {
        assert(ctx != nullptr);

        auto args = _parse_args(argv, argc);
        const auto &path = args.path;

        auto key = api::open_key(ctx, args.key_name, api::KeyMode::WRITEONLY);
        assert(key);

        auto &module = RedisProtobuf::instance();

        long long len = 0;
        if (!api::key_exists(key.get(), module.type())) {
            auto msg = module.proto_factory()->create(path.type());
            FieldRef field(msg.get(), path);
            _append(field, args.elements);

            if (RedisModule_ModuleTypeSetValue(key.get(),
                        module.type(),
                        msg.get()) != REDISMODULE_OK) {
                throw Error("failed to set message");
            }

            msg.release();

            len = args.elements.size();
        } else {
            auto *msg = api::get_msg_by_key(key.get());
            assert(msg != nullptr);

            if (path.empty()) {
                throw Error("can only call append on array");
            }

            FieldRef field(msg, path);
            // TODO: create a new message, and append to that message, then swap to this message.
            _append(field, args.elements);

            len = field.array_size();
        }

        return RedisModule_ReplyWithLongLong(ctx, len);
    } catch (const WrongArityError &err) {
        return RedisModule_WrongArity(ctx);
    } catch (const Error &err) {
        return api::reply_with_error(ctx, err);
    }

    return REDISMODULE_ERR;
}

AppendCommand::Args AppendCommand::_parse_args(RedisModuleString **argv, int argc) const {
    assert(argv != nullptr);

    if (argc < 4) {
        throw WrongArityError();
    }

    Args args;
    args.key_name = argv[1];
    args.path = Path(argv[2]);
    args.elements.reserve(argc - 3);

    for (auto idx = 3; idx != argc; ++idx) {
        args.elements.emplace_back(argv[idx]);
    }

    return args;
}

void AppendCommand::_append(FieldRef &field, const std::vector<StringView> &elements) const {
    if (!field.is_array() || field.is_array_element()) {
        throw Error("not an array");
    }

    for (const auto &ele : elements) {
        _append(field, ele);
    }
}

void AppendCommand::_append(FieldRef &field, const StringView &val) const {
    assert(field.is_array() && !field.is_array_element());

    switch (field.type()) {
    case gp::FieldDescriptor::CPPTYPE_INT32:
        field.add_int32(util::sv_to_int32(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_INT64:
        field.add_int64(util::sv_to_int64(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_UINT32:
        field.add_uint32(util::sv_to_uint32(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_UINT64:
        field.add_uint64(util::sv_to_uint64(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_DOUBLE:
        field.add_double(util::sv_to_double(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_FLOAT:
        field.add_float(util::sv_to_float(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_BOOL:
        field.add_bool(util::sv_to_bool(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_STRING:
        field.add_string(util::sv_to_string(val));
        break;

    case gp::FieldDescriptor::CPPTYPE_MESSAGE:
        _add_msg(field, val);
        break;

    default:
        throw Error("unknown type");
    }
}

void AppendCommand::_add_msg(FieldRef &field, const StringView &val) const {
    auto msg = RedisProtobuf::instance().proto_factory()->create(field.msg_type(), val);
    assert(msg);

    field.add_msg(*msg);
}

}

}

}