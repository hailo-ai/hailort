import argparse
import hef_pb2
import struct
import hashlib

HEADER_VERSION = 1
CCW_OFFSET_FORMAT = ">II"
CCW_NOP_VALUE = b'\x00\x00\x00\x00\x00\x00\x00\x00'


class HefParsingError(Exception):
    pass


class HefWriterError(Exception):
    pass


# Keep updated to hef_internals.hpp and DFC
class HefHeader(object):
    HEF_HEADER_FORMAT = ">IIII16s"
    HEF_HEADER_SIZE = struct.calcsize(HEF_HEADER_FORMAT)
    HEF_HEADER_MAGIC = 0x01484546
    HEF_HEADER_VERSION = 0

    def __init__(self, raw_header: bytes):
        self.magic, self.version, self.proto_size, self.ccws_size, self.proto_md5_raw = struct.unpack(self.HEF_HEADER_FORMAT, raw_header)
        self.proto_md5 = self.proto_md5_raw.hex()
        if self.magic != self.HEF_HEADER_MAGIC:
            raise HefParsingError(f"Bad header magic {self.magic} (expected {self.HEF_HEADER_MAGIC}")
        if self.version != self.HEF_HEADER_VERSION:
            raise HefParsingError(f"Bad header version {self.version} (expected {self.HEF_HEADER_VERSION}")

    def serialize(self):
        return struct.pack(self.HEF_HEADER_FORMAT, self.magic, self.version, self.proto_size, self.ccws_size, self.proto_md5_raw)


class HefWriter(object):
    # TODO: Change allowed archs to Hailo10
    ALLOWED_HW_ARCHS = { hef_pb2.ProtoHEFHwArch.PROTO__HW_ARCH__HAILO15H }
    CSM_PAGE_SIZE = 512

    @staticmethod
    def _read_hef(hef_path):
        with open(hef_path, "rb") as fd:
            data = fd.read()
        return data

    def __init__(self, path: str):
        data = self._read_hef(path)
        self.header = HefHeader(data[:HefHeader.HEF_HEADER_SIZE])
        hef_proto_raw = data[HefHeader.HEF_HEADER_SIZE:]
        self._validate_header(self.header, hef_proto_raw)

        self.hef_proto = hef_pb2.ProtoHEFHef()
        self.hef_proto.ParseFromString(hef_proto_raw)
        self._validate_proto_header(self.hef_proto.header)

    def _validate_header(self, header: HefHeader, hef_proto_raw: bytes):
        if header.proto_size != len(hef_proto_raw):
            raise HefParsingError(f"Bad header proto size {header.proto_size} "
                                  f"(actual remaining size {len(hef_proto_raw)}")
        proto_md5 = hashlib.md5(hef_proto_raw).hexdigest()
        if header.proto_md5 != proto_md5:
            raise HefParsingError(f"Bad header proto md5 {header.proto_size} (actual calculated md5 {proto_md5}")

    def _validate_proto_header(self, proto_header: hef_pb2.ProtoHEFHeader):
        if proto_header.hw_arch not in self.ALLOWED_HW_ARCHS:
            raise HefParsingError(f"Bad hw_arch {hef_pb2.ProtoHEFHwArch.Name(proto_header.hw_arch)} "
                                  f"(allowed hw_archs {[hef_pb2.ProtoHEFHwArch.Name(arch) for arch in self.ALLOWED_HW_ARCHS]}")

    def write_to_file(self, path: str):
        buffer_per_cfg_channel = self._get_buffer_per_cfg_channel()
        total_ccws_size = self._update_protobuf(buffer_per_cfg_channel)
        hef_proto_raw = self.hef_proto.SerializeToString()

        ccws = self._get_ccws(total_ccws_size, buffer_per_cfg_channel)
        with open(path, "wb") as f:
            self._write_header(f, hef_proto_raw, ccws)
            self._write_proto(f, hef_proto_raw)
            f.write(ccws)
    
    def _get_buffer_per_cfg_channel(self):
        buffer_per_cfg_channel = {}
        for chunk_index, ctx_index, action in CcwParser.get_write_ccw_actions_per_ctx(self.hef_proto):
            if ctx_index not in buffer_per_cfg_channel:
                buffer_per_cfg_channel[ctx_index] = {}

            cfg_channel_index = action.write_data_ccw.cfg_channel_index
            if cfg_channel_index not in buffer_per_cfg_channel[ctx_index]:
                buffer_per_cfg_channel[ctx_index][cfg_channel_index] = {}

            if chunk_index not in buffer_per_cfg_channel[ctx_index][cfg_channel_index]:
                buffer_per_cfg_channel[ctx_index][cfg_channel_index][chunk_index] = action.write_data_ccw.data
            else:
                buffer_per_cfg_channel[ctx_index][cfg_channel_index][chunk_index] += action.write_data_ccw.data
        return buffer_per_cfg_channel
    
    @staticmethod
    def _get_sizes_and_offsets(buffer_per_cfg_channel):
        sizes_and_offsets = {}
        offset = 0
        for i in HefWriter._iterate_over_numbered_dict(buffer_per_cfg_channel):
            sizes_and_offsets[i] = {}
            for j in HefWriter._iterate_over_numbered_dict(buffer_per_cfg_channel[i]):
                sizes_and_offsets[i][j] = {}
                cfg_channel_size = 0
                keys = [key for key in HefWriter._iterate_over_numbered_dict(buffer_per_cfg_channel[i][j])]
                for k in keys:
                    curr_size = len(buffer_per_cfg_channel[i][j][k])
                    cfg_channel_size += curr_size
                    if k == keys[-1]:
                        curr_size += HefWriter._get_padded_size(cfg_channel_size) - cfg_channel_size
                    sizes_and_offsets[i][j][k] = (offset, curr_size)
                    offset += curr_size
        return sizes_and_offsets
    
    def _update_protobuf(self, buffer_per_cfg_channel):
        total_ccws_size = 0
        sizes_and_offsets = HefWriter._get_sizes_and_offsets(buffer_per_cfg_channel)
        was_ccw_marked = {}
        for chunk_index, ctx_index, action in CcwParser.get_write_ccw_actions_per_ctx(self.hef_proto):
            if ctx_index not in was_ccw_marked:
                was_ccw_marked[ctx_index] = {}

            cfg_channel_index = action.write_data_ccw.cfg_channel_index
            if cfg_channel_index not in was_ccw_marked[ctx_index]:
                was_ccw_marked[ctx_index][cfg_channel_index] = []

            if chunk_index not in was_ccw_marked[ctx_index][cfg_channel_index]:
                was_ccw_marked[ctx_index][cfg_channel_index].append(chunk_index)

                offset, size = sizes_and_offsets[ctx_index][cfg_channel_index][chunk_index]
                action.write_data_ccw.data = struct.pack(CCW_OFFSET_FORMAT, offset, size)
                total_ccws_size += size
            else:
                action.write_data_ccw.data = b""
        return total_ccws_size
    
    @staticmethod
    def _iterate_over_numbered_dict(d):
        for i in range(max(d.keys()) + 1):
            if i not in d:
                continue
            else:
                yield i

    def _write_header(self, f, hef_proto_raw, ccws):
        self.header.version = HEADER_VERSION
        self.header.proto_size = len(hef_proto_raw)
        self.header.ccws_size = len(ccws)
        self.header.proto_md5_raw = bytes.fromhex(hashlib.md5(hef_proto_raw + ccws).hexdigest())
        f.write(self.header.serialize())

    def _write_proto(self, f, hef_proto_raw):
        f.write(hef_proto_raw)

    def _get_ccws(self, total_ccws_size, buffer_per_cfg_channel):
        ccws = bytearray(total_ccws_size)
        offset = 0
        for i in HefWriter._iterate_over_numbered_dict(buffer_per_cfg_channel):  # per context
            for j in HefWriter._iterate_over_numbered_dict(buffer_per_cfg_channel[i]):  # per config channel index
                chunks_size = 0
                keys = [key for key in HefWriter._iterate_over_numbered_dict(buffer_per_cfg_channel[i][j])]
                for k in keys:  # per chunk
                    if k == keys[-1]:
                        buffer = HefWriter._pad_ccws_with_nops(buffer_per_cfg_channel[i][j][k], chunks_size)
                    else:
                        buffer = buffer_per_cfg_channel[i][j][k]
                    chunks_size += len(buffer)
                    ccws[offset:offset + len(buffer)] = buffer
                    offset += len(buffer)
        return ccws

    @staticmethod
    def _get_padded_size(ccws_len):
        residue = ccws_len % HefWriter.CSM_PAGE_SIZE
        if residue == 0:
            return ccws_len

        padding_needed = HefWriter.CSM_PAGE_SIZE - residue
        return ccws_len + padding_needed

    @staticmethod
    def _pad_ccws_with_nops(ccws, added_size):
        ccw_nop = CCW_NOP_VALUE
        total_size = (added_size + len(ccws))
        residue = total_size % HefWriter.CSM_PAGE_SIZE
        if residue == 0:
            return ccws

        padding_needed = HefWriter.CSM_PAGE_SIZE - residue
        if padding_needed % len(ccw_nop) != 0:
            raise HefWriterError(f"Amount of padding needed ({padding_needed} B) "
                                 f"isn't a multiple of ccw nop size ({len(ccw_nop)} B)")
        nops_needed = padding_needed // len(ccw_nop)
        # Nop padding is done at the start of the ccw buffer, so that the csm's credits will be finished once the last ccw is processed
        return (ccw_nop * nops_needed) + ccws


class CcwParser(object):
    @staticmethod
    def get_write_ccw_actions_per_ctx(hef_proto):
        chunks_count = 0
        got_non_ccw = True
        last_operation_id = 0
        got_new_operation = True
        for context_index, action, operation_id in CcwParser._iterate_all_actions(hef_proto):
            action_type = action.WhichOneof("action")
            if "write_data_ccw" == action_type:
                if last_operation_id != operation_id:
                    last_operation_id = operation_id
                    got_new_operation = True
                if got_non_ccw or got_new_operation:
                    chunks_count += 1
                    got_non_ccw = False
                    got_new_operation = False
                yield (chunks_count - 1, context_index, action)
            else:
                got_non_ccw = True

    @staticmethod
    def _iterate_all_actions(hef_proto):
        operation_id = 0
        for network_group in hef_proto.network_groups:
            for operation in network_group.preliminary_config.operation:
                for action in operation.actions:
                    yield (0, action, operation_id)
                operation_id += 1
            for context in network_group.contexts:
                for operation in context.operations:
                    for action in operation.actions:
                        yield (context.context_index + 1, action, operation_id)
                    operation_id += 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("hef_path", metavar="hef-path")
    parser.add_argument("shef_path", metavar="shef-path")
    args = parser.parse_args()

    hef_writer = HefWriter(args.hef_path)
    hef_writer.write_to_file(args.shef_path)


if __name__ == "__main__":
    main()
