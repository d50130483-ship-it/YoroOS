import pycdlib
import os

iso = pycdlib.PyCdlib()
iso.new(sys_ident='YOROOS', vol_ident='YOROOS')

with open('build/os.img', 'rb') as f:
    data = f.read()

iso.add_fp(open('build/os.img', 'rb'), len(data), '/OS.IMG;1')

iso.add_eltorito('/OS.IMG;1', media_name='floppy')

iso.write('build/yoroos.iso')
iso.close()
