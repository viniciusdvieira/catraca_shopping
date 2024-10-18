from flask import Flask, flash, render_template, request, jsonify, redirect, url_for
import requests
from flask_sqlalchemy import SQLAlchemy
from flask_login import (
    LoginManager,
    UserMixin,
    login_user,
    login_required,
    logout_user,
    current_user
)

app = Flask(__name__)

# Configura novos delimitadores para evitar conflito (opcional)
# app.jinja_env.variable_start_string = '[[ '
# app.jinja_env.variable_end_string = ' ]]'

# Configurações do banco de dados
app.config['SECRET_KEY'] = 'sua_chave_secreta_aqui'  # Substitua por uma chave secreta real
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///usuarios.db'
db = SQLAlchemy(app)

# Configuração do Login Manager
login_manager = LoginManager()
login_manager.init_app(app)
login_manager.login_view = 'login'

# Substitua pelo endereço IP real do seu Arduino
ARDUINO_IP = 'http://192.168.3.138'

# Estado inicial das vagas
vagas = {
    'vaga1': {
        'status': 'livre',  # Pode ser 'livre', 'ocupada' ou 'reservada'
        'color': 'green'
    },
    'vaga2': {
        'status': 'livre',
        'color': 'green'
    }
}

# Modelo do usuário
class User(UserMixin, db.Model):
    id = db.Column(db.Integer, primary_key=True)
    username = db.Column(db.String(150), unique=True, nullable=False)
    senha = db.Column(db.String(150), nullable=False)
    rfid_uid = db.Column(db.String(50), unique=True)


# Função para carregar o usuário
@login_manager.user_loader
def load_user(user_id):
    return User.query.get(int(user_id))

# Rota para cadastro
@app.route('/cadastro', methods=['GET', 'POST'])
def cadastro():
    if request.method == 'POST':
        username = request.form.get('username')
        senha = request.form.get('senha')

        # Cria um novo usuário sem aplicar hash à senha
        novo_usuario = User(
            username=username,
            senha=senha
        )
        db.session.add(novo_usuario)
        db.session.commit()
        return redirect(url_for('login'))

    return render_template('cadastro.html')

# Rota para login
@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        senha = request.form.get('senha')

        user = User.query.filter_by(username=username).first()
        if not user or user.senha != senha:
            return 'Credenciais inválidas, tente novamente.'

        login_user(user)
        return redirect(url_for('index'))

    return render_template('login.html')

# Rota para logout
@app.route('/logout')
def logout():
    logout_user()
    return redirect(url_for('login'))

# Rota principal, sem proteção de login
@app.route('/')
def index():
    try:
        # Faz uma requisição GET à API do Arduino
        response = requests.get(f"{ARDUINO_IP}/vaga")
        data = response.json()

        # Atualiza o estado das vagas com base nos dados do Arduino
        vaga1_status = data.get("estado vaga 1", "vaga 1 livre")
        vaga2_status = data.get("estado vaga 2", "vaga 2 livre")

        # Atualiza o estado e a cor da vaga 1, somente se não estiver reservada
        if vagas['vaga1']['status'] != 'reservada':
            if 'livre' in vaga1_status:
                vagas['vaga1']['status'] = 'livre'
                vagas['vaga1']['color'] = 'green'
            elif 'ocupada' in vaga1_status:
                vagas['vaga1']['status'] = 'ocupada'
                vagas['vaga1']['color'] = 'red'

        # Atualiza o estado e a cor da vaga 2, somente se não estiver reservada
        if vagas['vaga2']['status'] != 'reservada':
            if 'livre' in vaga2_status:
                vagas['vaga2']['status'] = 'livre'
                vagas['vaga2']['color'] = 'green'
            elif 'ocupada' in vaga2_status:
                vagas['vaga2']['status'] = 'ocupada'
                vagas['vaga2']['color'] = 'red'

    except Exception as e:
        # Em caso de erro, mantém o estado atual
        print(f"Erro ao conectar à API: {e}")

    return render_template('index.html', vagas=vagas)

# Rota para reservar vaga, sem proteção de login
@app.route('/reservar', methods=['POST'])
def reservar():
    vaga_id = request.json.get('vaga_id')
    if vaga_id in vagas:
        # Verifica se a vaga está livre antes de reservar
        if vagas[vaga_id]['status'] == 'livre':
            # Envia uma requisição ao Arduino para atualizar o estado de reserva
            try:
                if vaga_id == 'vaga1':
                    response = requests.post(f"{ARDUINO_IP}/reservar_vaga1")
                elif vaga_id == 'vaga2':
                    response = requests.post(f"{ARDUINO_IP}/reservar_vaga2")
                # Verifica se a requisição foi bem-sucedida
                if response.status_code == 200:
                    vagas[vaga_id]['status'] = 'reservada'
                    vagas[vaga_id]['color'] = 'blue'
                    return jsonify({'success': True, 'message': f'{vaga_id} reservada com sucesso!'})
                else:
                    return jsonify({'success': False, 'message': 'Falha ao comunicar com o Arduino.'})
            except Exception as e:
                print(f"Erro ao conectar ao Arduino: {e}")
                return jsonify({'success': False, 'message': 'Erro ao comunicar com o Arduino.'})
        else:
            return jsonify({'success': False, 'message': f'{vaga_id} não está disponível para reserva.'})
    else:
        return jsonify({'success': False, 'message': 'Vaga inválida.'})
    
@app.route('/registrar_cartao', methods=['GET', 'POST'])
@login_required
def registrar_cartao():
    if request.method == 'POST':
        rfid_uid = request.form.get('rfid_uid')

        # Verifica se o cartão já está registrado
        existing_user = User.query.filter_by(rfid_uid=rfid_uid).first()
        if existing_user:
            flash('Este cartão já está registrado a outro usuário.', 'danger')
            return redirect(url_for('registrar_cartao'))

        # Atualiza o usuário atual com o UID do cartão
        current_user.rfid_uid = rfid_uid
        db.session.commit()
        flash('Cartão RFID registrado com sucesso!', 'success')
        return redirect(url_for('index'))

    return render_template('registrar_cartao.html')

@app.route('/verificar_cartao', methods=['GET'])
def verificar_cartao():
    rfid_uid = request.args.get('rfid_uid')
    if rfid_uid:
        user = User.query.filter_by(rfid_uid=rfid_uid).first()
        if user:
            return 'valido', 200
    return 'invalido', 401

@app.route('/abrir_catraca', methods=['GET', 'POST'])
@login_required
def abrir_catraca():
    if request.method == 'POST':
        # Enviar requisição ao Arduino para abrir a catraca
        try:
            response = requests.post(f"{ARDUINO_IP}/abrir_catraca")
            if response.status_code == 200:
                flash('Catraca aberta com sucesso!', 'success')
            else:
                flash('Falha ao comunicar com o dispositivo.', 'danger')
        except Exception as e:
            print(f"Erro ao conectar ao dispositivo: {e}")
            flash('Erro ao comunicar com o dispositivo.', 'danger')
        return redirect(url_for('index'))

    return render_template('abrir_catraca.html')

@app.route('/perdeu_cartao', methods=['GET', 'POST'])
def perdeu_cartao():
    if request.method == 'POST':
        username = request.form.get('username')
        senha = request.form.get('senha')

        user = User.query.filter_by(username=username).first()
        if user and user.senha == senha:
            # Autenticação bem-sucedida
            login_user(user)
            flash('Autenticação bem-sucedida. Você pode abrir a catraca.', 'success')
            return redirect(url_for('abrir_catraca'))
        else:
            flash('Credenciais inválidas. Tente novamente.', 'danger')
            return redirect(url_for('perdeu_cartao'))

    return render_template('perdeu_cartao.html')

# Rota para liberar vaga, sem proteção de login
@app.route('/liberar', methods=['POST'])
def liberar():
    vaga_id = request.json.get('vaga_id')
    if vaga_id in vagas:
        # Verifica se a vaga está reservada antes de liberar
        if vagas[vaga_id]['status'] == 'reservada':
            # Envia uma requisição ao Arduino para liberar a vaga
            try:
                if vaga_id == 'vaga1':
                    response = requests.post(f"{ARDUINO_IP}/liberar_vaga1")
                elif vaga_id == 'vaga2':
                    response = requests.post(f"{ARDUINO_IP}/liberar_vaga2")
                # Verifica se a requisição foi bem-sucedida
                if response.status_code == 200:
                    vagas[vaga_id]['status'] = 'livre'
                    vagas[vaga_id]['color'] = 'green'
                    return jsonify({'success': True, 'message': f'{vaga_id} liberada com sucesso!'})
                else:
                    return jsonify({'success': False, 'message': 'Falha ao comunicar com o Arduino.'})
            except Exception as e:
                print(f"Erro ao conectar ao Arduino: {e}")
                return jsonify({'success': False, 'message': 'Erro ao comunicar com o Arduino.'})
        else:
            return jsonify({'success': False, 'message': f'{vaga_id} não está reservada.'})
    else:
        return jsonify({'success': False, 'message': 'Vaga inválida.'})

if __name__ == '__main__':
    # Cria as tabelas no banco de dados, se não existirem
    with app.app_context():
        db.create_all()
    app.run(host='0.0.0.0', port=5000, debug=True)

