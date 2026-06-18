import onnx
import numpy as np
from onnx import numpy_helper

def fuse_bn_manually(input_path, output_path):
    print(f"[INFO] Loading model from: {input_path}")
    model = onnx.load(input_path)
    graph = model.graph
    
    initializers = {init.name: init for init in graph.initializer}
    
    print("[INFO] Searching for Conv + Mul + Add patterns (Decomposed BN)...")
    
    # Run multiple passes to fuse all sequential patterns completely
    for _ in range(4):
        new_removed = set()
        for i in range(len(graph.node)):
            node = graph.node[i]
            if node.op_type == 'Identity':
                # Bypass Identity: connect input directly to subsequent nodes
                input_name = node.input[0]
                output_name = node.output[0]
                for next_node in graph.node:
                    for idx, ipt in enumerate(next_node.input):
                        if ipt == output_name:
                            next_node.input[idx] = input_name
                new_removed.add(node.name)
                continue
            
            # Pattern 1: Conv -> Mul
            if node.op_type == 'Mul':
                input_name = node.input[0]
                prev_node = next((n for n in graph.node if input_name in n.output and n.op_type == 'Conv'), None)
                if prev_node:
                    mul_const_name = node.input[1]
                    if mul_const_name in initializers:
                        print(f"[INFO] Fusing Mul '{node.name}' into Conv '{prev_node.name}'")
                        mul_val = numpy_helper.to_array(initializers[mul_const_name]).flatten().astype(np.float32)
                        w_name = prev_node.input[1]
                        W = numpy_helper.to_array(initializers[w_name]).astype(np.float32)
                        W_new = (W * mul_val.reshape(-1, 1, 1, 1)).astype(np.float32)
                        initializers[w_name].CopyFrom(numpy_helper.from_array(W_new, w_name))
                        if len(prev_node.input) > 2:
                            b_name = prev_node.input[2]
                            B = numpy_helper.to_array(initializers[b_name]).astype(np.float32)
                            B_new = (B * mul_val).astype(np.float32)
                            initializers[b_name].CopyFrom(numpy_helper.from_array(B_new, b_name))
                        else:
                            B_new = np.zeros(W.shape[0], dtype=np.float32)
                            new_b_name = w_name + "_bias"
                            graph.initializer.append(numpy_helper.from_array(B_new, new_b_name))
                            initializers[new_b_name] = graph.initializer[-1]
                            prev_node.input.append(new_b_name)
                        node.op_type = 'Identity'
                        if len(node.input) > 1: del node.input[1:]

            # Pattern 2: Conv -> Add
            if node.op_type == 'Add':
                input_name = node.input[0]
                prev_node = next((n for n in graph.node if input_name in n.output and n.op_type == 'Conv'), None)
                if prev_node:
                    add_const_name = node.input[1]
                    if add_const_name in initializers:
                        print(f"[INFO] Fusing Add '{node.name}' into Conv '{prev_node.name}'")
                        add_val = numpy_helper.to_array(initializers[add_const_name]).flatten().astype(np.float32)
                        if len(prev_node.input) > 2:
                            b_name = prev_node.input[2]
                            B = numpy_helper.to_array(initializers[b_name]).astype(np.float32)
                            B_new = (B + add_val).astype(np.float32)
                            initializers[b_name].CopyFrom(numpy_helper.from_array(B_new, b_name))
                        else:
                            w_name = prev_node.input[1]
                            B_new = add_val.astype(np.float32)
                            new_b_name = w_name + "_bias"
                            graph.initializer.append(numpy_helper.from_array(B_new, new_b_name))
                            initializers[new_b_name] = graph.initializer[-1]
                            prev_node.input.append(new_b_name)
                        node.op_type = 'Identity'
                        if len(node.input) > 1: del node.input[1:]

            # Pattern 3: Clip -> Relu
            if node.op_type == 'Clip':
                node.op_type = 'Relu'
                if len(node.input) > 1: del node.input[1:]

        # Delete bypassed Identity nodes
        nodes_to_keep = [n for n in graph.node if n.name not in new_removed]
        del graph.node[:]
        graph.node.extend(nodes_to_keep)
    
    # Final pass: Delete unused Initializers to reduce model size
    active_inputs = set()
    for n in graph.node: active_inputs.update(n.input)
    initializers_to_keep = [i for i in graph.initializer if i.name in active_inputs]
    del graph.initializer[:]
    graph.initializer.extend(initializers_to_keep)
    
    print(f"[INFO] Saving fused model to: {output_path}")
    onnx.save(model, output_path)
    print("[SUCCESS] Batch Normalization manual fusion completed successfully.")

if __name__ == "__main__":
    fuse_bn_manually("models/mobilenetv2_128d_v3.onnx", "models/mobilenetv2_128d_v3_sim.onnx")
