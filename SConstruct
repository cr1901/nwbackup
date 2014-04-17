mtcp_root = Dir('#/../..')

tcp_inc_dir = mtcp_root.Dir('TCPINC')
tcp_c_dir = mtcp_root.Dir('TCPLIB')
common_inc_dir = mtcp_root.Dir('INCLUDE')
#jsmn_dir = Dir('#/jsmn')
test_dir = Dir('#/TEST')

mtcp_cpp = 'packet.cpp arp.cpp eth.cpp ip.cpp tcp.cpp ' \
	'tcpsockm.cpp udp.cpp utils.cpp dns.cpp timer.cpp ipasm.asm'

#nwbackup_src = 'nwbackup.cpp msbackup.c'
nwbackup_src = ''

#test_src = [test_dir.File(s) for s in Split('dirtest.c')]
#print test_src
#test_env = DefaultEnvironment()

env = Environment(tools = ['watcom'], USEWASM=True)
env['MEMMODEL16'] = 'L'
env['ASFLAGS'] = '-zq -m${MEMMODEL} -0'
env.Append(CPPDEFINES = [('CFG_H', '\"nwbackup.cfg\"')])
env.Append(CPPPATH = [Dir('#'), tcp_inc_dir]) 
env.Append(CCFLAGS = '-bt=dos -d2 -ecw')
env.Append(LINKFLAGS = 'system dos debug all')
	
#env.Append(CCFLAGS = '--pedantic')
	
#conf = Configure(env)

env.VariantDir('.', tcp_c_dir, False) #Includes a directory, but compiles in current.
mtcp_objs = env.Object(Split(mtcp_cpp))
nwbackup_objs = env.Object(Split(nwbackup_src))
nwbackup_exe = env.Program(nwbackup_objs + mtcp_objs)


dir_test = env.Program([test_dir.File('dirtest.c'), 'dir.c'])

test_exes = [dir_test]
env.Alias('tests', test_exes)

Default(mtcp_objs + nwbackup_exe)
