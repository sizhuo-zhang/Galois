import bmk2
from bmkprops import graph_bmk, PERF_RE, get_ktruss_checker
import os

class KtrussGaloisBSP(graph_bmk):
    bmk = "ktruss"
    variant = "galois+bsp"

    def filter_inputs(self, inputs):
        def finput(x):
            if not "symmetric" in x.props.flags: return False
            if x.props.format == 'bin/galois': return True

            return False

        return filter(finput, inputs)

    def get_run_spec(self, bmkinput):
        x = bmk2.RunSpec(self, bmkinput)
        k = 3

        x.set_binary(self.props._cwd, 'k-truss')
        x.set_arg(bmkinput.props.file, bmk2.AT_INPUT_FILE)
        x.set_arg('-algo=bsp', bmk2.AT_OPAQUE)
        x.set_arg('-trussNum=%d' % (k,), bmk2.AT_OPAQUE)
        x.set_arg("-t=%d" % (1,), bmk2.AT_OPAQUE)
        x.set_arg('-o=@output', bmk2.AT_TEMPORARY_OUTPUT)
        x.set_checker(bmk2.ExternalChecker(get_ktruss_checker(bmkinput.props.file, k)))

        x.set_perf(bmk2.PerfRE(r"^\(NULL\),.*, Time,0,0,(?P<time_ms>[0-9]+)$"))
        return x

        
BINARIES = [KtrussGaloisBSP()]