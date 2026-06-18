import onnx
import sys

def fix_onnx_pads(onnx_path):
    model = onnx.load(onnx_path)
    graph = model.graph
    fixed = 0
    
    for node in graph.node:
        # 1. Fix Pad nodes directly
        if node.op_type == 'Pad':
            found_mode = False
            for attr in node.attribute:
                if attr.name == 'mode':
                    found_mode = True
                    if attr.s != b'constant':
                        print(f"[FIX] Fixing Pad {node.name}: {attr.s} -> constant")
                        attr.s = b'constant'
                        fixed += 1
            if not found_mode:
                node.attribute.extend([onnx.helper.make_attribute("mode", "constant")])
                fixed += 1
        
        # 2. Fix padding attributes in Convolution layers (if non-standard modes exist)
        if node.op_type in ['Conv', 'ConvTranspose']:
            # ESP-DL requires padding to be explicit integer coordinates, not auto_pad strings like 'SAME_UPPER'
            for attr in node.attribute:
                if attr.name == 'auto_pad' and attr.s != b'NOTSET':
                    print(f"[FIX] Fixing Conv {node.name} auto_pad: {attr.s} -> NOTSET")
                    attr.s = b'NOTSET'
                    fixed += 1

    onnx.save(model, onnx_path)
    print(f"[SUCCESS] ONNX pads fix complete. Modified {fixed} items in {onnx_path}")

if __name__ == "__main__":
    fix_onnx_pads("models/mobilenetv2_128d_v3.onnx")
