#! /usr/bin/python3
import time
#time.sleep(30)
import datetime

from telegram.ext import Updater
from telegram.ext import CommandHandler
import logging
import datetime
import subprocess

import json
import threading
from threading import Timer

import ipgetter

# Imports for the data analysis
import numpy as np
import pandas as pd

#Imports for the Platform API
from requests_oauthlib import OAuth2Session
from oauthlib.oauth2 import BackendApplicationClient
from requests.auth import HTTPBasicAuth
import json
import paho.mqtt.client as mqtt

import requests
import os
from os.path import join, dirname
from dotenv import load_dotenv
dotenv_path = join(dirname(__file__), '.env')
a=load_dotenv(dotenv_path)


TOKEN = os.environ.get("TELEGRAM_KEY")
URL = "https://api.telegram.org/bot{}/".format(TOKEN)


#Defining the URLS
api_url= 'https://api.demo.konkerlabs.net/v1/'
applications_url = api_url + 'applications/'

#Defining Device URL
device_url='https://konker.io/device?deviceID='

#Defining the Location of non-installed devices
non_installed_location= 'Pool'

#Setting up the credencials
client_id = os.environ.get("CLIENT_ID")
client_secret = os.environ.get("CLIENT_SECRET")
token_url = api_url+'oauth/token?grant_type=client_credentials'


#Hardcoded application
app='default'

#Conecting...
auth = HTTPBasicAuth(client_id, client_secret)
client = BackendApplicationClient(client_id=client_id)
oauth = OAuth2Session(client=client)
token = oauth.fetch_token(token_url=token_url, auth=auth)
response = oauth.get(applications_url)



current_milli_time = lambda: int(round(time.time() * 1000))


#Function to get the data (hardcoded to 240 samples
def getdatafromchannel(application, devicename, channel):
    urlapp = "https://api.demo.konkerlabs.net/v1/" + application + "/"
    urldevices = urlapp + "devices/"
    response = oauth.get(urldevices)
    r = response.json()

    guid = ''
    for i in r['result']:
        if i['id'] == devicename:
            guid = i['guid']
            break
    if guid == '':
        return

    try:
        response = oauth.get(urlapp + 'incomingEvents?q=device:' + guid + ' channel:' + channel + '&limit=1')
        r = response.json()
    except:
        print('Could not get the data!!')
    print(r)   

    return r['result'][0].get('payload').get('Caixa').upper() + "\n(Última atualização às " + datetime.datetime.fromtimestamp(int(r['timestamp'])).strftime('%H:%M:%S %d-%m-%Y') +")"






def caixa(bot, update):
    status = getdatafromchannel(app, 'smartCelo', 'caixa')
    bot.sendMessage(chat_id=update.message.chat_id, text="Olá a caixa de correspondências da Konker está: %s " % status)

def ip(bot, update):
    ip = subprocess.check_output(["/bin/hostname","-I"]).decode('utf-8')
    bot.sendMessage(chat_id=update.message.chat_id, text=("My IP: %s" % ip))
    p_ip = ipgetter.myip()
    bot.sendMessage(chat_id=update.message.chat_id, text=("My Public IP: %s" % p_ip))



def error_callback(bot, update, error):
    logger.error(error)
    updater.stop()
    sys.exit(1)

def get_url(url):
    response = requests.get(url)
    content = response.content.decode("utf8")
    return content

def get_json_from_url(url):
    content = get_url(url)
    js = json.loads(content)
    return js


def get_updates(offset=None):
    url = URL + "getUpdates?timeout=100"
    if offset:
        url += "&offset={}".format(offset)
    js = get_json_from_url(url)
    return js


logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s', level=logging.DEBUG)
logger = logging.getLogger("SmarCelo")



#updater = Updater(token='420349570:AAGfsMfzukwFSwQHqhPlBeOwQFQ1wSKA8FM')
#dispatcher = updater.dispatcher
#dispatcher.add_error_handler(error_callback)

start_handler = CommandHandler('caixa', caixa)
#dispatcher.add_handler(start_handler)

ip_handler = CommandHandler('ip', ip)
#dispatcher.add_handler(ip_handler)


logger.info("Starting polling...")
#updater.start_polling()
#updater.idle()

def get_last_update_id(updates):
    update_ids = []
    for update in updates["result"]:
        update_ids.append(int(update["update_id"]))
    return max(update_ids)


def send_message(text, chat_id):
    url = URL + "sendMessage?text={}&chat_id={}".format(text, chat_id)
    get_url(url)

def echo_all(updates):
    for update in updates["result"]:
        try:
            text = update["message"]["text"]
            chat = update["message"]["chat"]["id"]
            #send_message(text, chat)

            if text=="/caixa":
                status = getdatafromchannel(app, 'smartCelo', 'caixa')
                send_message("Olá a caixa de correspondências da Konker está: %s " % status, chat)
            if text=="/start":
                send_message("Olá, eu sou o bot da caixa de correspondências da Konker.\nDigite /caixa para saber o status da caixa.", chat)

        except Exception as e:
            print(e)

def main():
    last_update_id = None
    while True:
        updates = get_updates(last_update_id)
        if len(updates["result"]) > 0:
            last_update_id = get_last_update_id(updates) + 1
            echo_all(updates)
        time.sleep(0.5)

main()