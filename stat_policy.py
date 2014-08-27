import xml.etree.ElementTree as ElementTree
import numpy as np
import os, sys
import numpy.random as npr
import corpus
import itertools

class PolicyResult:
  def __init__(me, name):
    me.name = name
    me.tree = ElementTree.parse(me.name+'/policy.xml')
    me.root = me.tree.getroot()
    me.param = dict()
    for data in me.root:
      if data.tag == 'args':
        for item in data:
          if item.tag == 'corpus':
            me.corpus = item.text.replace('\n', '')
      if data.tag == 'train':
	pass
      if data.tag == 'test':
        for item in data:
          if item.tag == 'param':
            me.param = dict()
            for attr in item:
              if attr.tag == 'entry':
                me.param[attr.attrib['name']] = float(attr.attrib['value'])
          elif item.tag == 'accuracy':
            me.accuracy = float(item.text)
          elif item.tag == 'time':
            me.time = float(item.text)
          elif item.tag == 'example':
            me.parse_test(item)
          
  def parse_test_datapoint(me, node):
    exp = dict()
    for attr in node:
      if attr.tag == 'dist' or attr.tag == 'time':
        exp[attr.tag] = float(attr.text)
      elif attr.tag == 'feat':
        feat = dict()
        for entry in attr:
          feat[entry.attrib['name']] = float(entry.attrib['value'])
        if not exp.has_key('feat'):
          exp['feat'] = list();
        exp['feat'].append(feat)
      else:
        exp[attr.tag] = unicode(attr.text)
    return exp 

  def parse_test(me, node):
    me.testex = list()
    for row in node:
      ex = list()
      exp = me.parse_test_datapoint(row) 
      if exp.has_key('tag'):
        ex.append(exp)
      for attr in row:
        if attr.tag.find('pass') == 0:
          ex.append(me.parse_test_datapoint(attr))
      me.testex.append(ex)

  def ave_time(me):
    if hasattr(me, 'time'):
      return me.time
    time = list()
    for ex in me.testex:
      ex_len = len(ex['truth'].split('\t'))-1
      # time.append(ex['time'] * ex_len)
      time.append(ex['time'])
    # print time
    return np.mean(time)

  # compare with you, and visualize the result.
  @staticmethod
  def viscomp(policy_l, name_l, mode='POS'):
    tagprob = None
    for policy in policy_l:
      if hasattr(policy, 'corpus'):
        tagprob = corpus.read_tagposterior(policy.corpus, mode)
        break
    BG_HIGHLIGHT = '#D4E6FA'
    BG_SELECT = '#EDD861'
    for policy in policy_l:
      if len(policy.testex) != len(policy_l[0].testex):
        raise Exception("testCount not the same.") 
    head = "<style>"
    head += '''td {
      transition: padding-top 0.1s;
      padding-top: 5px;
    }
    td:hover {
      padding-top: 0px;
    }
    td span: {
      transition: background-color 0.3s; 
      background-color: #FFFFFF;
    }
    td span:hover{
      background-color: %s;
    }''' % BG_SELECT
    head += '</style>'
    body = ['''<p>Locations selected for inference are highlighted with <span style='background-color: %s'>&nbsp;&nbsp;&nbsp;</span> <br>
    Features have their means subtracted.  </p>''' % BG_HIGHLIGHT]
    body += ['''<h3> Models </h3> <table>''']
    for (name, policy) in zip(name_l, policy_l):
      body += ['''<tr><td><span title="%s">%s</span></td></tr> '''%(str(policy.param), name)]
    body += '''</table> <h3> Test Examples </h3>'''
    count = 0
    oldname_l = name_l
    for ex_l in zip(*[test.testex for test in policy_l]):
      name_l = list()
      for (name, ex) in zip(oldname_l,ex_l):
        name_l += [name] + ['   '] * (len(ex)-1)
      ex_l = list(itertools.chain(*ex_l))
      token_truth = ex_l[0]['truth'].replace('\n', '').split('\t')
      token_l = [ex['tag'].replace('\n', '').split('\t') for ex in ex_l]
      words = [token.split('/')[0] for token in token_truth if token != '']
      true_tag = [token.split('/')[1] for token in token_truth if token != '']
      tag_l = [[token.split('/')[1] for token in token0 if token != ''] for token0 in token_l]
      mask_l = list()
      for ex in ex_l:
        if ex.has_key('mask'):
          mask_l.append([float(t) for t in ex['mask'].replace('\n', '').split('\t') if t != ''])
        else:
          mask_l.append(None)
      body += ["<p><table><tr>"]
      body += ['''<td nowrap style='background-color: #000000; color: #ffffff'>%d</td>''' % count]
      body += ['''<td nowrap style='text-align: center; color: #9C9C9C; font-size: 14'><b style='font-size: 16'> Words</b> <br>
      Truth ''']
      for name in name_l:
        body += ['''<br> %s ''' % name]
      body += ['''</td>''']
      for (i, token) in enumerate(zip(*([words, true_tag]+tag_l))):
        token = list(token)
        w = token[0]
        tag = token[1]
        token = token[2:]
        f = list()
        all_same = True
        for j in range(len(token)):
          if token[j] != tag:
            all_same = False
          if tagprob != None:
            if tagprob.has_key(words[i]):
              ex_l[j]['feat'][i]['prob'] = tagprob[words[i]]
            else:
              ex_l[j]['feat'][i]['prob'] = 'not seen'
          f.append(ex_l[j]['feat'][i])
        if all_same:
          c = ['#000000'] * len(token)
        else:
          c = list()
          for j in range(len(token)):
            if token[j] == tag:
              c.append('11B502')
            else:
              c.append('ED2143')
        body += ['''<td nowrap style='text-align: center; font-size: 14'> <b style='font-size: 16'> %s </b> <br> 
        <span> %s </span>''' % (w, tag)]
        for j in range(len(token)):
          bg = ''
          if mask_l[j] != None and mask_l[j][i] == 1:
            bg = '; background-color: %s ' % BG_HIGHLIGHT
          body += ['''<br> <span title=\"%s\" style='display: inline-block; color: %s %s'> %s </span>''' % (str(f[j]), c[j], bg, token[j])]
        body += ['''</td>''']

      body += ["</tr></table></p>"]
      count += 1
    return '''<html>\n<head>\n%s</head>\n<body>\n%s</body>\n</html>''' % (head, "".join(body))

if __name__ == '__main__':
  name = sys.argv[1]
  if name == '__viscomp__':
    policy_g1 = PolicyResult('test_policy/gibbs_T1')
    policy_g2 = PolicyResult('test_policy/gibbs_T2')
    policy_thres1 = PolicyResult('test_policy/entropy_1.00')
    policy_thres2 = PolicyResult('test_policy/entropy_2.00')
    policy_cyclic = PolicyResult('test_policy/cyclic_c0.1_K3')
    print PolicyResult.viscomp([policy_g1, policy_g2, policy_thres2, policy_thres1, policy_cyclic], ['Pass 1', 'Pass 2', \
    'Thres 2.0','Thres 1.0', 'cyclic'])
  else:
    stop = PolicyResult(name)
    print 'time = ', stop.ave_time(), 'accuracy = ', stop.accuracy
